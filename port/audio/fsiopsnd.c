/* FSIOPSND, FromSoftware's sound driver on the IOP (RPC server 0x1010), on the PC.

   Written from the disc's FSIOPSND.IRX (which kept its symbol table: the names below are its own).
   It drives Sony's CSL modules (csl.h) and libsd (sd.h): sequences on four MIDI ports, a
   synthesizer with sixteen ports fed by the sequences and by the game's sound-effect messages, and
   streamed music (.ADS files) on each core's sound data input.

   The IOP ran it as threads; here each thread is the code it ran between two waits, run when the
   PS2 would have woken it, all on EE time:
   - the RPC thread: port_fsiopsnd_call, at the EE time of the call;
   - the tick thread: woken by a hardware timer every 4167 us, it runs the sequencer, the
     synthesizer and the driver's song ends (fsiopsnd_tick, run by the kernel at its EE time);
   - the ADS stream thread: woken by the sound data input each time it has read half of a stream's
     ring, it refills that half from the disc (ads_proc, from inside the SPU2's render);
   - the ADS set thread: one pass each time the game polls 0x30B0 (ads_set_proc).
   The PS2's own timing between threads (a thread starting a little after its wakeup) is not kept;
   what each one does, and in which order, is.

   Behaviour the PS2 has and this keeps: the replies, including the IOP addresses the game is given
   (the driver's buffers sit where FSIOPSND's own did), and the retail bugs that the game reaches.
   Where the PS2 would hang (adsPreset, reached only by commands KF4 never sends), this does not. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csl.h"
#include "port/audio.h"
#include "port/platform.h"
#include "port/savestate.h"
#include "sd.h"
#include "sifdev.h"
#include "sifrpc.h"

/* The tick: USec2SysClock(4167 us) at the IOP's 36.864 MHz is 153612 IOP cycles, 4 EE cycles each */
#define TICK_USEC 4167
#define TICK_EE_CYCLES (153612u * 4u)

/* Where FSIOPSND's buffers were in IOP memory (the module loaded at 0x5B800): the game is given these
   addresses and sends data to them */
#define MODULE_BASE 0x5B800u
#define SE_STREAM_ADDR (MODULE_BASE + 0x75D0u)  /* gSeStreamBf, 0x400 bytes */
#define PACKET_ADDR (MODULE_BASE + 0x79D0u)     /* gPacket, 0x4000 bytes */
#define SE_STREAM_SIZE 0x400
#define PACKET_SIZE 0x4000

/* seqState: the low nibble is the port's state, 0x10 makes its pans centre */
enum { SEQ_EMPTY, SEQ_STOPPED, SEQ_PLAYING, SEQ_PAUSED };
#define SEQ_STATE_MASK 0x0F
#define SEQ_FORCE_MONO 0x10

enum { TICK_BLOCKED, TICK_RUN, TICK_QUIT };

/* ---- ADS streams ----------------------------------------------------------------------------- */
enum {
    ADS_CLOSED = 0,
    ADS_OPENED = 1,
    ADS_PRESET = 2,
    ADS_PLAYING = 3,
    ADS_STOPPED = 4,
    ADS_PAUSED = 5,
    ADS_HDRDONE = 6,
};
#define ADS_ASYNC 0x10
#define ADS_SET_PENDING (ADS_ASYNC | ADS_OPENED)
#define ADS_SET_PRELOAD (ADS_ASYNC | ADS_HDRDONE)

#define ADSF_PAUSE_REQ 0x0001
#define ADSF_VOL_REQ 0x0002
#define ADSF_STOP_REQ 0x0004
#define ADSF_LOOP 0x0080
#define ADSF_DRAIN 0x0100
#define ADSF_HALF_DONE 0x8000

#define ADS_BODY_OFS_FILE 0x28   /* the body follows the 0x28-byte header */
#define ADS_BODY_OFS_SECT 0x800  /* or, with format bit 31, starts at the second sector */

/* An open file on the disc, read as the IOP's ioman reads it */
typedef struct {
    int open;
    uint32_t lsn;
    uint32_t size;
    uint32_t position;
} IopFile;

typedef struct {
    uint8_t header[0x28];   /* the file's first 0x28 bytes: format at +8, body size at +0x24 */
    char path[0x40];
    uint32_t buf;           /* the ring's IOP address: two halves of `half` bytes */
    int32_t half;
    int32_t pos;            /* body bytes read so far */
    int fd;                 /* 0 or 1 when open (the channel's file), -1 not */
    int32_t state;
    int32_t flags;
    uint16_t volL;
    uint16_t volR;
} Ads;

static uint32_t ads_format(const Ads *a) {
    uint32_t v;

    memcpy(&v, a->header + 8, 4);
    return v;
}

static int32_t ads_body_size(const Ads *a) {
    int32_t v;

    memcpy(&v, a->header + 0x24, 4);
    return v;
}

/* ---- The driver's state (FSIOPSND's gSndMng and the rest of its .bss) ------------------------ */
static struct {
    CslCtx midiCtx;
    CslBuffGrp midiGrp[2];
    CslBuffCtx midiIn[8];
    CslBuffCtx midiOut[4];
    MidiEnv midiPort[4];
    int32_t loopCount[4];      /* -1: stop at the end; 0: loop for ever; n: n more plays */
    uint8_t seqState[4];
    uint16_t relTempo[4];

    CslCtx hsynCtx;
    CslBuffGrp hsynGrp[1];
    CslBuffCtx hsynIn[32];
    HSynEnv hsynPort[16];
    CslMidiStream stream[16];

    int initialised;           /* the timer is running */
    int tickState;
    uint64_t nextTick;
    int adsThreadAlive;        /* the ADS stream thread exists */
    int adsDrainChannel;       /* the stream thread is waiting out a channel's last half (-1: not) */
    int adsLast;               /* the channel it served last */
    int adsSetThread;          /* the ADS set thread exists */
    int adsSetActive;          /* and has not exited */
    uint16_t masterVol;
    uint16_t stereo;

    Ads ads[2];
    IopFile files[2];
    HSynVoiceStat voiceStat;   /* gVoiStat */
    uint8_t chStat[0x10];      /* gHSynChStat */
    uint8_t voiceNoBySeId[0x40];
    uint8_t rpcArg[0x100];     /* gRpcArg: what the call sent */
    int32_t ret;
} w;

int fsiopsnd_init(void);

static uint8_t *se_stream(void) {
    return port_iop_ptr(SE_STREAM_ADDR);
}

/* ---- Files (ioman on cdrom0:) ---------------------------------------------------------------- */
/* "cdrom0:\DATA\TITLE.ADS;1" (any device prefix) to the file on the disc */
static int file_open(int fd, const char *path) {
    IopFile *f = &w.files[fd];
    const char *colon = strchr(path, ':');

    memset(f, 0, sizeof(*f));
    if (!port_disc_find(colon != NULL ? colon + 1 : path, &f->lsn, &f->size)) {
        port_event("FSIOPSND: no file %s on the disc", path);
        return -1;
    }
    f->open = 1;
    return fd;
}

static void file_close(int fd) {
    if (fd >= 0) {
        w.files[fd].open = 0;
    }
}

static void file_seek(int fd, uint32_t position) {
    w.files[fd].position = position;
}

/* Reads n bytes into IOP memory; the count read (short at the end of the file) */
static int file_read(int fd, void *dst, int n) {
    IopFile *f = &w.files[fd];
    uint8_t sector[DISC_SECTOR_SIZE];
    uint8_t *out = dst;
    int done = 0;

    if (!f->open || n <= 0) {
        return 0;
    }
    if ((uint32_t)n > f->size - f->position) {
        n = (int)(f->size - f->position);
    }
    while (done < n) {
        uint32_t offset = f->position % DISC_SECTOR_SIZE;
        uint32_t chunk = DISC_SECTOR_SIZE - offset;

        if (chunk > (uint32_t)(n - done)) {
            chunk = (uint32_t)(n - done);
        }
        if (offset == 0 && chunk == DISC_SECTOR_SIZE) {
            if (!port_disc_read(f->lsn + f->position / DISC_SECTOR_SIZE, 1, out + done)) {
                break;
            }
        } else {
            if (!port_disc_read(f->lsn + f->position / DISC_SECTOR_SIZE, 1, sector)) {
                break;
            }
            memcpy(out + done, sector + offset, chunk);
        }
        f->position += chunk;
        done += (int)chunk;
    }
    return done;
}

static uint8_t *ads_half(const Ads *a, int index) {
    return port_iop_ptr(a->buf + (uint32_t)(a->half * index));
}

/* ---- The tick ----------------------------------------------------------------------------------- */
/* After the sequencer and synthesizer: a song that reached its end stops, or starts again */
static void fsSnd_ATick(void) {
    int port;

    w.tickState = TICK_BLOCKED;
    for (port = 3; port >= 0; port--) {
        int ended = (w.midiPort[port].status & MIDI_ST_DATAEND) != 0;

        if (w.loopCount[port] != -1) {
            if (!ended) {
                continue;
            }
            if (w.loopCount[port] == 1) {
                sceMidi_MidiPlaySwitch(&w.midiCtx, (unsigned)port, 0);
                w.seqState[port] = (uint8_t)((w.seqState[port] & 0xF0) | SEQ_STOPPED);
                w.loopCount[port] = -1;
            } else {
                sceMidi_SelectMidi(&w.midiCtx, (unsigned)port, 0);
                sceMidi_MidiSetLocation(&w.midiCtx, (unsigned)port, 0);
                sceMidi_MidiPlaySwitch(&w.midiCtx, (unsigned)port, 1);
                sceMidi_MidiSetRelativeTempo(&w.midiCtx, (unsigned)port, w.relTempo[port]);
                if (w.loopCount[port] >= 2) {
                    w.loopCount[port]--;
                }
            }
        } else if ((w.seqState[port] & SEQ_STATE_MASK) == SEQ_PLAYING && ended) {
            w.seqState[port] = (uint8_t)((w.seqState[port] & 0xF0) | SEQ_STOPPED);
        }
    }
    w.tickState = TICK_RUN;
}

/* KF4_STREAM_DUMP=path: the synthesizer's input streams at each tick, before it reads them (one
   line a tick: the tick number, the vblank, then s<n>:<hex> for each stream with data), and a line at
   each sound-effect flush, for comparison with the PS2 driver's */
static FILE *stream_dump_file;
static int stream_dump_checked;
static unsigned long stream_dump_ticks;

static FILE *stream_dump_open(void) {
    if (!stream_dump_checked) {
        const char *path = getenv("KF4_STREAM_DUMP");

        stream_dump_checked = 1;
        if (path != NULL && *path != '\0') {
            stream_dump_file = fopen(path, "w");
        }
    }
    return stream_dump_file;
}

static void stream_dump(void) {
    FILE *f = stream_dump_open();
    int i;

    if (f == NULL) {
        return;
    }
    fprintf(f, "%lu %llu", ++stream_dump_ticks, (unsigned long long)port_vblank_count());
    for (i = 0; i < 16; i++) {
        uint32_t k;

        if (w.stream[i].validsize == 0) {
            continue;
        }
        fprintf(f, " s%d:", i);
        for (k = 0; k < w.stream[i].validsize && k < CSL_STREAM_DATA; k++) {
            fprintf(f, "%02x", w.stream[i].data[k]);
        }
    }
    fputc('\n', f);
}

uint64_t fsiopsnd_next_tick(void) {
    return w.initialised ? w.nextTick : UINT64_MAX;
}

/* One tick of the driver's timer (the tick thread's work while the state is RUN) */
static void tick_body(void) {
    if (w.tickState == TICK_QUIT) {
        w.initialised = 0;
        return;
    }
    if (w.tickState != TICK_RUN) {
        return;
    }
    sceMidi_ATick(&w.midiCtx);
    stream_dump();
    sceHSyn_ATick(&w.hsynCtx);
    fsSnd_ATick();
}

/* The device's clock (port/audio/output.c, the audio thread, under the lock): every 4167 us of sound */
void fsiopsnd_tick_device(void) {
    if (w.initialised) {
        tick_body();
    }
}

/* Back on the game's clock: the next tick a period from now */
void fsiopsnd_resume_ticks(uint64_t now) {
    w.nextTick = now + TICK_EE_CYCLES;
}

void fsiopsnd_tick(uint64_t now) {
    w.nextTick += TICK_EE_CYCLES;
    /* the timer only wakes the tick thread while the state is RUN */
    if (w.tickState == TICK_QUIT) {
        w.initialised = 0;
        return;
    }
    if (w.tickState != TICK_RUN) {
        return;
    }
    sd_set_time(now);
    sd_trace_mark("%llu %llu TICK", (unsigned long long)port_vblank_count(), (unsigned long long)now);
    sceMidi_ATick(&w.midiCtx);
    stream_dump();
    sceHSyn_ATick(&w.hsynCtx);
    fsSnd_ATick();
}

/* modmidi's channel-message callbacks: pans are centred when the output is mono or the port is
   marked for it */
static uint32_t chMsgFilter(int port, uint32_t msg) {
    if (w.stereo != 0 && !(w.seqState[port] & SEQ_FORCE_MONO)) {
        return msg;
    }
    if ((msg & 0xFFF0) == 0x0AB0) {
        msg = (msg & 0xFFFF) + 0x3F0000;
    }
    return msg;
}

static uint32_t chMsgCB0(uint32_t msg, void *priv) {
    (void)priv;
    return chMsgFilter(0, msg);
}

static uint32_t chMsgCB1(uint32_t msg, void *priv) {
    (void)priv;
    return chMsgFilter(1, msg);
}

static uint32_t chMsgCB2(uint32_t msg, void *priv) {
    (void)priv;
    return chMsgFilter(2, msg);
}

static uint32_t chMsgCB3(uint32_t msg, void *priv) {
    (void)priv;
    return chMsgFilter(3, msg);
}

/* ---- The ADS stream thread ---------------------------------------------------------------------- */
static void ads_dma_off(int ch) {
    sd_block_trans(ch, SD_TRANS_STOP, 0, 0);
    sd_set_param((uint16_t)(ch | SD_PARAM_BVOLL), 0);
    sd_set_param((uint16_t)(ch | SD_PARAM_BVOLR), 0);
}

static int ads_pick(int last) {
    int p0 = (w.ads[0].flags & ADSF_HALF_DONE) != 0;
    int p1 = (w.ads[1].flags & ADSF_HALF_DONE) != 0;

    if (p0 && last != 0) {
        return 0;
    }
    if (p1 && last != 1) {
        return 1;
    }
    if (p0) {
        return 0;
    }
    if (p1) {
        return 1;
    }
    return -1;
}

/* One wakeup of the stream thread: refill the half of a ring that has just been read */
static void ads_proc(void) {
    int ch;
    int idx;
    int32_t remain;
    int n;
    Ads *a;

    if (w.adsDrainChannel >= 0) {
        /* the wakeup the thread waited for after queueing a stream's last, zero-padded half */
        ch = w.adsDrainChannel;
        w.adsDrainChannel = -1;
        ads_dma_off(ch);
        w.ads[ch].state = ADS_STOPPED;
        w.ads[ch].flags = 0;
        return;
    }
    if (w.ads[0].state == ADS_CLOSED && w.ads[1].state == ADS_CLOSED) {
        w.adsThreadAlive = 0;
        return;
    }
    ch = ads_pick(w.adsLast);
    if (ch < 0) {
        return;
    }
    w.adsLast = ch;
    a = &w.ads[ch];
    a->flags &= 0x7FFF;
    /* the half the input is on now is in bits 24 and up: refill the other one */
    idx = 1 - (int)(sd_block_trans_status(ch, 0) >> 24);

    if (a->flags & ADSF_STOP_REQ) {
        ads_dma_off(ch);
        a->state = ADS_STOPPED;
        a->flags = 0;
        return;
    }
    if (a->flags & ADSF_DRAIN) {
        w.adsDrainChannel = ch;
        return;
    }
    if (a->flags & ADSF_VOL_REQ) {
        sd_set_param((uint16_t)(ch | SD_PARAM_BVOLL), a->volL);
        sd_set_param((uint16_t)(ch | SD_PARAM_BVOLR), a->volR);
        a->flags &= ~ADSF_VOL_REQ;
    }

    remain = ads_body_size(a) - a->pos;
    if (a->half < remain) {
        n = file_read(a->fd, ads_half(a, idx), a->half);
        if (n < a->half) {
            return; /* a short read leaves the half as it was */
        }
        a->pos += n;
    } else {
        n = file_read(a->fd, ads_half(a, idx), remain);
        if (n < remain) {
            return;
        }
        if (a->flags & ADSF_LOOP) {
            /* back to the start of the body for the rest of this half */
            file_seek(a->fd, (ads_format(a) & 0x80000000u) ? ADS_BODY_OFS_SECT : ADS_BODY_OFS_FILE);
            n = file_read(a->fd, ads_half(a, idx) + remain, a->half - remain);
            if (n != a->half - remain) {
                port_event("FSIOPSND: ADS channel %d cannot loop: the body is shorter than a half", ch);
            }
            a->pos = a->half - remain;
        } else {
            memset(ads_half(a, idx) + remain, 0, (size_t)((a->half - remain) & ~3));
            a->flags |= ADSF_HALF_DONE;
            a->flags |= ADSF_DRAIN;
        }
    }
    if (a->flags & ADSF_PAUSE_REQ) {
        ads_dma_off(ch);
        a->state = ADS_PAUSED;
        a->flags &= ~ADSF_PAUSE_REQ;
    }
}

/* The core's transfer callbacks: the input has read half of the ring */
static void ads_wakeup(int ch) {
    if (w.adsThreadAlive) {
        w.ads[ch].flags |= ADSF_HALF_DONE;
        ads_proc();
    }
}

static int adsCB0(void *data) {
    (void)data;
    ads_wakeup(0);
    return 1;
}

static int adsCB1(void *data) {
    (void)data;
    ads_wakeup(1);
    return 1;
}

static void ads_start_stream_thread(void) {
    if (!w.adsThreadAlive) {
        w.adsThreadAlive = 1;
        w.adsDrainChannel = -1;
        w.adsLast = 0; /* uninitialised in the IRX until its first pass */
    }
}

/* ---- ADS commands ------------------------------------------------------------------------------ */
/* Both halves (or the zero-padded body) from the file. FSIOPSND's adsPreset loops on this for ever;
   it is only reached by commands KF4 never sends. */
static int adsPreset(int ch) {
    Ads *a = &w.ads[ch & 0xF];

    if (a->state == ADS_CLOSED) {
        return -1;
    }
    port_event("FSIOPSND: adsPreset (it never returns on the PS2)");
    if (ads_format(a) & 0x80000000u) {
        file_seek(a->fd, ADS_BODY_OFS_SECT);
    }
    if (ads_body_size(a) < a->half * 2) {
        memset(ads_half(a, 0), 0, (size_t)(a->half * 2));
        file_read(a->fd, ads_half(a, 0), ads_body_size(a));
        a->pos = ads_body_size(a);
    } else {
        file_read(a->fd, ads_half(a, 0), a->half * 2);
        a->pos = a->half * 2;
    }
    a->state = ADS_PRESET;
    return 0;
}

static int adsOpen(int ch, const char *path) {
    Ads *a = &w.ads[ch];

    if (a->state != ADS_CLOSED) {
        return -1;
    }
    a->fd = file_open(ch, path);
    if (a->fd < 0) {
        return -1; /* the IRX retries the open for ever */
    }
    file_read(a->fd, a->header, 0x28);
    a->state = ADS_OPENED;
    a->pos = 0;
    a->flags = 0;
    ads_start_stream_thread();
    if ((ads_format(a) & 0xFFFF) != 0x10) {
        sd_set_trans_callback(ch, ch == 0 ? adsCB0 : adsCB1);
    }
    return 0;
}

static int adsSetBuff(int ch, int size) {
    Ads *a = &w.ads[ch & 0xF];
    int address;

    if (a->state != ADS_OPENED && a->state != ADS_CLOSED) {
        return -1;
    }
    /* AllocSysMemory: from the IOP's heap, as the game's own sceSifAllocIopHeap */
    address = sceSifAllocIopHeap(size);
    a->buf = (uint32_t)address;
    if (address != 0 && (uint32_t)address + (uint32_t)size <= 0x1FFFFF) {
        a->half = size >> 1;
        return 0;
    }
    return -1;
}

/* One pass of the set thread over both channels */
static void ads_set_proc(void) {
    int i;

    if (!(w.ads[0].state & ADS_ASYNC) && !(w.ads[1].state & ADS_ASYNC)) {
        w.adsSetActive = 0;
        return;
    }
    for (i = 0; i < 2; i++) {
        Ads *a = &w.ads[i];

        if (!(a->state & ADS_ASYNC)) {
            continue;
        }
        switch (a->state & 0xF) {
        case ADS_OPENED:
            if (a->fd < 0) {
                a->fd = file_open(i, a->path);
                break;
            }
            if (file_read(a->fd, a->header, 0x28) != 0x28) {
                file_seek(a->fd, 0);
            } else {
                a->state = ADS_SET_PRELOAD;
                if (ads_format(a) & 0x80000000u) {
                    file_seek(a->fd, ADS_BODY_OFS_SECT);
                }
            }
            break;
        case ADS_HDRDONE:
            if (ads_body_size(a) < a->half * 2) {
                memset(ads_half(a, 0), 0, (size_t)(a->half * 2));
                if (file_read(a->fd, ads_half(a, 0), ads_body_size(a)) != ads_body_size(a)) {
                    port_event("[fsiopsnd]preload failed.ch=%d retry...", i);
                    file_seek(a->fd, (ads_format(a) & 0x80000000u) ? ADS_BODY_OFS_SECT : ADS_BODY_OFS_FILE);
                } else {
                    a->state = ADS_PRESET;
                    a->pos = ads_body_size(a);
                }
            } else if (file_read(a->fd, ads_half(a, 0), a->half * 2) != a->half * 2) {
                file_seek(a->fd, (ads_format(a) & 0x80000000u) ? ADS_BODY_OFS_SECT : ADS_BODY_OFS_FILE);
            } else {
                a->state = ADS_PRESET;
                a->pos = a->half * 2;
            }
            break;
        default:
            break;
        }
    }
}

static int adsSet(int ch, const char *path) {
    Ads *a = &w.ads[ch];

    if (a->state != ADS_CLOSED) {
        return -1;
    }
    snprintf(a->path, sizeof(a->path), "%s", path);
    a->fd = file_open(ch, a->path);
    a->state = ADS_SET_PENDING;
    a->pos = 0;
    a->flags = 0;
    ads_start_stream_thread();
    /* the header is not read yet: this tests the previous stream's (retail does the same) */
    if (ads_format(a) != 0x10) {
        sd_set_trans_callback(ch, ch == 0 ? adsCB0 : adsCB1);
    }
    w.adsSetThread = 1;
    w.adsSetActive = 1;
    return 0;
}

static int adsSync(void) {
    if (w.adsSetActive == 1 && w.adsSetThread) {
        ads_set_proc();
        return 0;
    }
    if (w.adsSetThread) {
        w.adsSetActive = 0;
        w.adsSetThread = 0;
    }
    return 1;
}

static int adsStart(int ch, int loop) {
    Ads *a = &w.ads[ch];

    switch (a->state) {
    case ADS_PRESET:
    case ADS_PAUSED:
        break;
    case ADS_PLAYING:
    case ADS_STOPPED:
        sd_block_trans(ch, SD_TRANS_STOP, 0, 0);
        a->flags = 0;
        sd_set_param((uint16_t)(ch | SD_PARAM_BVOLL), 0);
        sd_set_param((uint16_t)(ch | SD_PARAM_BVOLR), 0);
        /* fall through */
    case ADS_OPENED:
        file_seek(a->fd, ADS_BODY_OFS_FILE);
        if (adsPreset(ch) != 0) {
            return -1;
        }
        break;
    default:
        return -1;
    }
    sd_block_trans(ch, SD_TRANS_WRITE | SD_TRANS_LOOP, a->buf, (uint32_t)(a->half * 2));
    sd_set_param((uint16_t)(ch | SD_PARAM_BVOLL), a->volL);
    sd_set_param((uint16_t)(ch | SD_PARAM_BVOLR), a->volR);
    a->state = ADS_PLAYING;
    if (loop == 1) {
        a->flags |= ADSF_LOOP;
    }
    return 0;
}

static int adsPause(int ch) {
    Ads *a = &w.ads[ch & 0xF];

    if (a->state != ADS_PLAYING) {
        return -1;
    }
    a->flags |= ADSF_PAUSE_REQ;
    return 0;
}

static int adsStop(int ch) {
    Ads *a = &w.ads[ch & 0xF];

    switch (a->state) {
    case ADS_PLAYING:
        a->flags |= ADSF_STOP_REQ;
        /* fall through */
    case ADS_PAUSED:
        ads_dma_off(ch);
        a->state = ADS_STOPPED;
        a->flags = 0;
        return 0;
    case ADS_CLOSED:
    case ADS_OPENED:
    case ADS_PRESET:
    case ADS_STOPPED:
        return -1;
    default:
        return 0;
    }
}

static int adsClose(int ch) {
    Ads *a = &w.ads[ch & 0xF];

    sd_set_param((uint16_t)((ch & 0xF) | SD_PARAM_BVOLL), 0);
    sd_set_param((uint16_t)((ch & 0xF) | SD_PARAM_BVOLR), 0);
    if (a->state == ADS_PLAYING) {
        /* the IRX waits here for the stream thread to stop the stream, which it does only at the end
           of a stream that does not loop: KF4 always stops first */
        port_event("FSIOPSND: closing a playing ADS stream (the PS2 waits for its end)");
        ads_dma_off(ch);
        a->state = ADS_STOPPED;
    }
    if (a->fd >= 0) {
        file_close(a->fd);
    }
    sd_set_trans_callback(ch & 0xF, NULL);
    if (a->buf != 0) {
        sceSifFreeIopHeap((int)a->buf);
    }
    a->state = ADS_CLOSED;
    if (w.ads[0].state == ADS_CLOSED && w.ads[1].state == ADS_CLOSED) {
        w.adsSetActive = 0;
        w.adsSetThread = 0;
        w.adsThreadAlive = 0;
        w.adsDrainChannel = -1;
    }
    return 0;
}

static int adsVolume(int ch, uint16_t l, uint16_t r) {
    Ads *a = &w.ads[ch & 0xF];

    a->volL = l;
    a->volR = r;
    if (a->state == ADS_PLAYING) {
        a->flags |= ADSF_VOL_REQ;
    }
    return 0;
}

/* ---- Sound-effect messages (0x1080) ------------------------------------------------------------- */
/* The game's list in gSeStreamBf: a byte count, then records {0xFF, stream, n, n bytes of MIDI
   messages}, appended to the synthesizer's input streams */
static int seFlush(void) {
    uint8_t *buffer = se_stream();
    int32_t length;

    w.tickState = TICK_BLOCKED;
    memcpy(&length, buffer, 4);
    if (length != 0) {
        uint8_t *p = buffer + 4;
        uint8_t *end = buffer + 4 + (length > SE_STREAM_SIZE - 4 ? SE_STREAM_SIZE - 4 : length);

        while (p < end) {
            CslMidiStream *s;
            int n;

            if (*p++ != 0xFF) {
                break;
            }
            s = &w.stream[*p++ & 15];
            n = *p++;
            /* retail checks against buffsize, which counts the header: up to 8 bytes past data[]
               fit, and are dropped here */
            if (s->buffsize < s->validsize + (uint32_t)n) {
                break;
            }
            if (s->validsize + (uint32_t)n > CSL_STREAM_DATA) {
                port_event("FSIOPSND: sound-effect stream %d overflows by %u bytes", (int)(s - w.stream),
                           (unsigned)(s->validsize + (uint32_t)n - CSL_STREAM_DATA));
                break;
            }
            memcpy(s->data + s->validsize, p, (size_t)n);
            s->validsize += (uint32_t)n;
            p += n;
        }
        length = 0;
        memcpy(buffer, &length, 4);
    }
    /* RUN afterwards even if the tick had not started yet */
    w.tickState = TICK_RUN;
    return 0;
}

/* ---- Sound bank headers (.hd) -------------------------------------------------------------------- */
static uint32_t rd32(const uint8_t *p, uint32_t offset) {
    uint32_t v;

    memcpy(&v, p + offset, 4);
    return v;
}

static uint16_t rd16(const uint8_t *p, uint32_t offset) {
    uint16_t v;

    memcpy(&v, p + offset, 2);
    return v;
}

static void wr16(uint8_t *p, uint32_t offset, uint16_t v) {
    memcpy(p + offset, &v, 2);
}

/* The samples of a program: calls visit(sample) for each sample its splits reach */
typedef void (*SampleVisit)(uint8_t *sample, void *arg);

static void hd_walk_program(uint8_t *hd, uint32_t prog, SampleVisit visit, void *arg) {
    uint8_t *header = hd + 0x10;
    uint8_t *programs = hd + rd32(header, 0x14);
    uint8_t *sets = hd + rd32(header, 0x18);
    uint8_t *samples = hd + rd32(header, 0x1C);
    int32_t offset = (int32_t)rd32(programs, 0x10 + prog * 4);
    uint8_t *p;
    uint8_t *split;
    int k;

    if (offset == -1) {
        return;
    }
    p = programs + offset;
    if (p[4] == 0) {
        return;
    }
    split = p + rd32(p, 0);
    for (k = p[4]; k > 0; k--, split += 0x14) {
        uint16_t set_index = rd16(split, 0);
        uint8_t *set;
        int j;

        if (rd32(sets, 0x0C) < set_index) {
            continue;
        }
        set = sets + rd32(sets, 0x10 + set_index * 4u);
        for (j = 0; j < set[3]; j++) {
            uint16_t sample = rd16(set, 4 + (uint32_t)j * 2);

            if (rd32(samples, 0x0C) >= sample) {
                visit(samples + rd32(samples, 0x10 + sample * 4u), arg);
            }
        }
    }
}

static void get_adsr(uint8_t *sample, void *arg) {
    *(uint32_t *)arg = ((uint32_t)rd16(sample, 0x12) << 16) | rd16(sample, 0x14);
}

static void set_adsr(uint8_t *sample, void *arg) {
    uint32_t adsr = *(uint32_t *)arg;

    wr16(sample, 0x12, (uint16_t)(adsr >> 16));
    wr16(sample, 0x14, (uint16_t)adsr);
}

static void centre_pan(uint8_t *sample, void *arg) {
    (void)arg;
    sample[0x0D] = 0x3F;
}

/* 0x1070: copies a header and centres every pan in the copy */
static int makeMonoHD(uint8_t *dst, const uint8_t *src, int size) {
    uint8_t *programs;
    uint32_t i;

    memmove(dst, src, (size_t)(size > 0 ? size : 0));
    programs = dst + rd32(dst + 0x10, 0x14);
    for (i = 0; i <= rd32(programs, 0x0C); i++) {
        int32_t offset = (int32_t)rd32(programs, 0x10 + i * 4);
        uint8_t *p;
        uint8_t *split;
        int k;

        if (offset == -1) {
            continue;
        }
        p = programs + offset;
        if (p[4] == 0) {
            continue;
        }
        p[7] = 0x3F;
        p[10] = 0;
        split = p + rd32(p, 0);
        for (k = p[4]; k > 0; k--, split += 0x14) {
            split[0x11] = 0x3F;
        }
        hd_walk_program(dst, i, centre_pan, NULL);
    }
    return 0;
}

/* ---- The RPC server -------------------------------------------------------------------------------- */
#define ARG(n) a[n]

static void *fsSndrFunc(unsigned int fno, void *data) {
    int32_t a[0x40];
    CslCtx *midi = &w.midiCtx;
    CslCtx *hsyn = &w.hsynCtx;
    int port;
    int i;

    memcpy(a, data, sizeof(a));
    w.ret = 0;
    switch (fno & 0xFFFF0) {
    case 0x10:
        w.ret = fsiopsnd_init();
        break;
    case 0x20:
        w.tickState = TICK_QUIT;
        break;
    case 0x30:
        w.masterVol = (uint16_t)ARG(1);
        sd_set_param(0 | SD_PARAM_MVOLL, 0x3FFF);
        sd_set_param(0 | SD_PARAM_MVOLR, 0x3FFF);
        sd_set_param(1 | SD_PARAM_MVOLL, w.masterVol);
        sd_set_param(1 | SD_PARAM_MVOLR, w.masterVol);
        break;
    case 0x40:
        for (port = 3; port >= 0; port--) {
            if ((w.seqState[port] & SEQ_STATE_MASK) == SEQ_PLAYING) {
                sceMidi_MidiPlaySwitch(midi, (unsigned)port, 1);
                sceMidi_MidiSetRelativeTempo(midi, (unsigned)port, w.relTempo[port]);
            }
        }
        if (w.tickState == TICK_BLOCKED) {
            w.tickState = TICK_RUN;
        }
        for (i = 1; i >= 0; i--) {
            if (w.ads[i].state == ADS_PLAYING) {
                sd_block_trans(i, SD_TRANS_WRITE | SD_TRANS_LOOP, w.ads[i].buf, (uint32_t)(w.ads[i].half * 2));
                sd_set_param((uint16_t)(i | SD_PARAM_BVOLL), w.ads[i].volL);
                sd_set_param((uint16_t)(i | SD_PARAM_BVOLR), w.ads[i].volR);
            }
            sd_set_param((uint16_t)(i | SD_PARAM_MVOLL), i != 0 ? w.masterVol : 0x3FFF);
            sd_set_param((uint16_t)(i | SD_PARAM_MVOLR), i != 0 ? w.masterVol : 0x3FFF);
        }
        break;
    case 0x50:
        for (i = 1; i >= 0; i--) {
            sd_set_param((uint16_t)(i | SD_PARAM_MVOLL), 0xFFFF);
            sd_set_param((uint16_t)(i | SD_PARAM_MVOLR), 0xFFFF);
            sd_block_trans(i, SD_TRANS_STOP, 0, 0);
            sd_set_param((uint16_t)(i | SD_PARAM_BVOLL), 0);
            sd_set_param((uint16_t)(i | SD_PARAM_BVOLR), 0);
        }
        for (port = 3; port >= 0; port--) {
            if ((w.seqState[port] & SEQ_STATE_MASK) == SEQ_PLAYING) {
                sceMidi_MidiPlaySwitch(midi, (unsigned)port, 0);
            }
        }
        break;
    case 0x80:
        w.ret = sd_voice_trans_status((int16_t)ARG(1), (int16_t)ARG(2));
        break;
    case 0x90:
        w.ret = sd_voice_trans((int16_t)ARG(1), (uint16_t)ARG(2), (uint32_t)ARG(3), (uint32_t)ARG(4),
                               (uint32_t)ARG(5));
        break;
    case 0xA0:
        w.ret = (int32_t)PACKET_ADDR;
        break;
    case 0xB0:
        if (ARG(2) > 0) {
            memmove(port_iop_ptr((uint32_t)ARG(1)), port_iop_ptr(PACKET_ADDR),
                    (size_t)(ARG(2) < PACKET_SIZE ? ARG(2) : PACKET_SIZE));
        }
        break;
    case 0xC0:
        w.stereo = (uint16_t)ARG(1);
        sceHSyn_SetOutputMode(w.stereo == 1 ? 1 : 0);
        break;
    case 0xD0:
        sd_set_param((uint16_t)ARG(1), (uint16_t)ARG(2));
        break;
    case 0xE0:
        w.ret = sd_block_trans((int16_t)ARG(1), (uint16_t)ARG(2), (uint32_t)ARG(3), (uint32_t)ARG(4));
        break;
    case 0xF0:
        w.ret = (int32_t)sd_block_trans_status((int16_t)ARG(1), (int16_t)ARG(2));
        break;
    case 0x100:
        sd_set_core_attr((uint16_t)ARG(1), (uint16_t)ARG(2));
        break;
    case 0x110: {
        uint32_t adsr = 0; /* retail leaves it as it was when no sample is reached */

        hd_walk_program(port_iop_ptr((uint32_t)ARG(1)), (uint32_t)ARG(3), get_adsr, &adsr);
        w.ret = (int32_t)adsr;
        break;
    }
    case 0x120: {
        uint32_t adsr = (uint32_t)ARG(4);

        hd_walk_program(port_iop_ptr((uint32_t)ARG(1)), (uint32_t)ARG(3), set_adsr, &adsr);
        break;
    }
    case 0x1000:
        w.ret = sceHSyn_VoiceTrans((short)ARG(1), (uint32_t)ARG(2), (uint32_t)ARG(3), (unsigned)ARG(4));
        break;
    case 0x1010:
        w.ret = sceHSyn_Load(hsyn, (unsigned)ARG(1), (uint32_t)ARG(2),
                             ARG(3) != 0 ? port_iop_ptr((uint32_t)ARG(3)) : NULL, (unsigned)ARG(4));
        break;
    case 0x81020:
        return &w.voiceStat;
    case 0x1030:
        w.ret = sceHSyn_AllNoteOff(hsyn, (unsigned)ARG(1));
        break;
    case 0x1040:
        w.hsynPort[ARG(1) & 15].priority = (uint8_t)ARG(2);
        break;
    case 0x1050:
        w.hsynPort[ARG(1) & 15].maxPolyphony = (uint8_t)ARG(2);
        break;
    case 0x1060: {
        int v = ARG(2);

        if (v != 0) {
            v = (v * 2) | 0xF;
        }
        w.ret = sceHSyn_SetVolume(hsyn, (unsigned)ARG(1), (unsigned short)v);
        break;
    }
    case 0x1070:
        w.ret = makeMonoHD(port_iop_ptr((uint32_t)ARG(1)), port_iop_ptr((uint32_t)ARG(2)), ARG(3));
        break;
    case 0x1080:
        if (stream_dump_open() != NULL) {
            fprintf(stream_dump_file, "CALL 0x1080 %llu\n", (unsigned long long)port_vblank_count());
        }
        w.ret = seFlush();
        break;
    case 0x1090:
        sceHSyn_GetChStat(hsyn, (unsigned)ARG(1), w.chStat);
        return w.chStat;
    case 0x1100:
        w.ret = sceHSyn_MSGetVoiceStateByID(hsyn, (unsigned)ARG(1), (unsigned char)ARG(2), w.voiceNoBySeId, 0x30);
        break;
    case 0x2000:
        port = ARG(1) & 3;
        w.midiIn[2 * port].buff = port_iop_ptr((uint32_t)ARG(2));
        w.stream[port].validsize = 0;
        if (sceMidi_Load(midi, (unsigned)port) != 0 || sceMidi_SelectMidi(midi, (unsigned)port, 0) != 0 ||
            sceMidi_MidiSetLocation(midi, (unsigned)port, 0) != 0) {
            w.ret = 1;
        } else {
            w.seqState[port] = SEQ_STOPPED;
            if (w.stereo == 0) {
                w.seqState[port] |= SEQ_FORCE_MONO;
            }
        }
        break;
    case 0x2010: {
        int s = w.seqState[ARG(1) & 3] & SEQ_STATE_MASK;

        w.ret = s < 2 ? 0 : 1;
        break;
    }
    case 0x2030: {
        MidiEnv *env;

        port = ARG(1) & 3;
        w.loopCount[port] = ARG(2);
        env = &w.midiPort[port];
        if (env->status & (MIDI_ST_INPLAY | MIDI_ST_DATAEND)) {
            sceMidi_MidiPlaySwitch(midi, (unsigned)port, 0);
            if (sceMidi_SelectMidi(midi, (unsigned)port, 0) != 0 ||
                sceMidi_MidiSetLocation(midi, (unsigned)port, 0) != 0) {
                w.ret = 1;
                break;
            }
        }
        sceHSyn_ResetAllControler(hsyn, (unsigned)port);
        w.ret = sceMidi_MidiPlaySwitch(midi, (unsigned)port, 1);
        w.seqState[port] = (uint8_t)((w.seqState[port] & 0xF0) | SEQ_PLAYING);
        w.ret |= sceMidi_MidiSetRelativeTempo(midi, (unsigned)port, w.relTempo[port]);
        break;
    }
    case 0x2040:
        port = ARG(1) & 3;
        w.ret = sceMidi_MidiPlaySwitch(midi, (unsigned)port, 0);
        if (sceMidi_SelectMidi(midi, (unsigned)port, 0) != 0 || sceMidi_MidiSetLocation(midi, (unsigned)port, 0) != 0) {
            w.ret = 1;
        } else {
            w.seqState[port] = (uint8_t)((w.seqState[port] & 0xF0) | SEQ_STOPPED);
        }
        break;
    case 0x2050:
        port = ARG(1) & 3;
        w.ret = sceMidi_MidiPlaySwitch(midi, (unsigned)port, 0);
        w.seqState[port] = (uint8_t)((w.seqState[port] & 0xF0) | SEQ_PAUSED);
        break;
    case 0x2060:
        port = ARG(1) & 3;
        w.ret = sceMidi_MidiPlaySwitch(midi, (unsigned)port, 1);
        w.seqState[port] = (uint8_t)((w.seqState[port] & 0xF0) | SEQ_PLAYING);
        w.ret |= sceMidi_MidiSetRelativeTempo(midi, (unsigned)port, w.relTempo[port]);
        break;
    case 0x2070:
        port = ARG(1);
        w.ret = sceHSyn_Load(hsyn, (unsigned)port, (uint32_t)ARG(2),
                             ARG(3) != 0 ? port_iop_ptr((uint32_t)ARG(3)) : NULL, (unsigned)ARG(4));
        /* retail indexes the four sequence ports' states with a synthesizer port */
        if (port >= 0 && port < 4) {
            if (ARG(5) == 0) {
                w.seqState[port] |= SEQ_FORCE_MONO;
            } else {
                w.seqState[port] &= 0x0F;
            }
        } else {
            port_event("FSIOPSND: 0x2070 with port %d writes past the sequence states on the PS2", port);
        }
        break;
    case 0x2080: {
        uint16_t rel = (uint16_t)ARG(2);
        unsigned abs_tempo;

        port = ARG(1) & 3;
        abs_tempo = sceMidi_MidiGetAbsoluteTempo(midi, (unsigned)port);
        if (abs_tempo == 0) {
            abs_tempo = 20;
        }
        if ((int)rel < (int)(0x1400 / abs_tempo)) {
            rel = (uint16_t)(0x1400 / abs_tempo);
        }
        if ((int)(0x40000 / abs_tempo) < (int)rel) {
            rel = (uint16_t)(0x40000 / abs_tempo);
        }
        w.ret = sceMidi_MidiSetRelativeTempo(midi, (unsigned)port, rel);
        w.relTempo[port] = rel;
        break;
    }
    case 0x3000:
        w.rpcArg[sizeof(w.rpcArg) - 1] = 0;
        w.ret = adsOpen((int)(fno & 1), (const char *)w.rpcArg);
        break;
    case 0x3010:
        w.ret = adsPreset(ARG(1));
        break;
    case 0x3020:
        w.ret = adsStart(ARG(1) & 1, ARG(2));
        break;
    case 0x3030:
        w.ret = adsClose(ARG(1) & 1);
        break;
    case 0x3040:
        w.ret = adsSetBuff(ARG(1) & 1, ARG(2));
        break;
    case 0x3050:
        w.ret = adsStop(ARG(1) & 1);
        break;
    case 0x3060:
        w.ret = adsPause(ARG(1) & 1);
        break;
    case 0x3070:
        w.ret = adsVolume(ARG(1) & 1, (uint16_t)ARG(2), (uint16_t)ARG(3));
        break;
    case 0x3080:
        sd_set_param((uint16_t)((fno & 1) | SD_PARAM_MMIX), ARG(1) == 1 ? 0xFFC : 0xFCC);
        break;
    case 0x3090: {
        int s = w.ads[ARG(1) & 1].state & 0xF;

        w.ret = (s >= 1 && s <= 3) || (s >= 5 && s <= 6);
        break;
    }
    case 0x30A0:
        w.rpcArg[sizeof(w.rpcArg) - 1] = 0;
        w.ret = adsSet((int)(fno & 1), (const char *)w.rpcArg);
        break;
    case 0x30B0:
        w.ret = adsSync();
        break;
    case 0x30C0:
        /* streams by sector number: KF4 sends none */
        port_event("FSIOPSND: 0x30C0 (adsSetWithLsn) is not supported");
        w.ret = -1;
        break;
    case 0x40070: {
        HSynEffectAttr attr;

        /* _volume_down: retail sweeps core 1's master volume to 0 and waits for it; the EE time of a
           call does not move, so it is set to 0 at once */
        sd_set_param(1 | SD_PARAM_MVOLL, 0);
        sd_set_param(1 | SD_PARAM_MVOLR, 0);
        for (i = 1; i >= 0; i--) {
            sd_block_trans(i, SD_TRANS_STOP, 0, 0);
        }
        memcpy(&attr, w.rpcArg, sizeof(attr));
        w.ret = sceHSyn_SetEffectAttr(&attr);
        /* _volume_up */
        sd_set_param(1 | SD_PARAM_MVOLR, w.masterVol);
        sd_set_param(1 | SD_PARAM_MVOLL, w.masterVol);
        for (i = 1; i >= 0; i--) {
            if (w.ads[i].state == ADS_PLAYING) {
                sd_block_trans(i, SD_TRANS_WRITE | SD_TRANS_LOOP, w.ads[i].buf, (uint32_t)(w.ads[i].half * 2));
            }
        }
        break;
    }
    default:
        port_event("FSIOPSND: unknown command 0x%X", fno);
        w.ret = -1;
        break;
    }
    return &w.ret;
}

/* 0x10: libsd and the SPU2's mix, the two CSL modules, the ADS channels, then the timer */
int fsiopsnd_init(void) {
    static const MidiChMsgCallBack callbacks[4] = {chMsgCB0, chMsgCB1, chMsgCB2, chMsgCB3};
    int i;
    int j;

    sd_init(0);
    sd_set_addr(SPU2_SD_ADDR_EEA | 0, 0x1DFFFF);
    sd_set_addr(SPU2_SD_ADDR_EEA | 1, 0x1FFFFF);
    sd_set_param(SD_PARAM_MMIX | 0, 0xFC0);
    sd_set_param(SD_PARAM_MMIX | 1, 0xFCC);
    for (i = 0; i < 2; i++) {
        sd_set_param((uint16_t)(i | SD_PARAM_EVOLL), 0);
        sd_set_param((uint16_t)(i | SD_PARAM_EVOLR), 0);
        sd_set_param((uint16_t)(i | SD_PARAM_BVOLL), 0);
        sd_set_param((uint16_t)(i | SD_PARAM_BVOLR), 0);
    }
    sd_clear_effect_work_area(1, 0, 7 /* SD_EFFECT_MODE_ECHO */);
    w.masterVol = 0x3FFF;

    w.midiCtx.extmod = NULL;
    w.midiCtx.callBack = NULL;
    w.midiCtx.buffGrpNum = 2;
    w.midiCtx.buffGrp = w.midiGrp;
    w.midiGrp[0].buffNum = 8;
    w.midiGrp[0].buffCtx = w.midiIn;
    w.midiGrp[1].buffNum = 4;
    w.midiGrp[1].buffCtx = w.midiOut;
    for (i = 0; i < 4; i++) {
        w.midiIn[2 * i].sema = 0;
        w.midiIn[2 * i].buff = NULL;
        w.midiIn[2 * i + 1].sema = 0;
        w.midiIn[2 * i + 1].buff = &w.midiPort[i];
        w.midiOut[i].sema = 0;
        w.midiOut[i].buff = &w.stream[i];
        w.loopCount[i] = -1;
        w.seqState[i] = 0;
        w.relTempo[i] = 0x100;
    }
    if (sceMidi_Init(&w.midiCtx, TICK_USEC) != 0) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        w.midiPort[i].excOutPort = (uint16_t)(1 << i);
        for (j = 0; j < 16; j++) {
            w.midiPort[i].outPort[j] = (uint16_t)(1 << i);
        }
        w.midiPort[i].chMsgCallBack = callbacks[i];
        w.midiPort[i].chMsgCallBackPrivateData = &w.stream[i];
        w.midiPort[i].metaMsgCallBackPrivateData = &w.stream[i];
        w.midiPort[i].repeatCallBackPrivateData = &w.stream[i];
    }
    w.stereo = 1;

    w.hsynCtx.extmod = NULL;
    w.hsynCtx.callBack = NULL;
    w.hsynCtx.buffGrpNum = 1;
    w.hsynCtx.buffGrp = w.hsynGrp;
    w.hsynGrp[0].buffNum = 0x20;
    w.hsynGrp[0].buffCtx = w.hsynIn;
    for (i = 15; i >= 0; i--) {
        w.hsynIn[2 * i].sema = 0;
        w.hsynIn[2 * i].buff = &w.stream[i];
        w.hsynIn[2 * i + 1].sema = 0;
        w.hsynIn[2 * i + 1].buff = &w.hsynPort[i];
        w.hsynPort[i].priority = 0;
        w.hsynPort[i].maxPolyphony = 0;
        w.hsynPort[i].portMode = 0;
        w.hsynPort[i].waveType = 0;
        w.hsynPort[i].lfoWaveTbl = NULL;
        w.hsynPort[i].lfoWaveNum = 0;
        w.hsynPort[i].velocityMapTbl = NULL;
        w.hsynPort[i].velocityMapNum = 0;
    }
    if (sceHSyn_Init(&w.hsynCtx, TICK_USEC) != 0) {
        return -1;
    }
    for (i = 15; i >= 0; i--) {
        sceHSyn_SetVolume(&w.hsynCtx, (unsigned)i, 0x20F);
        sceHSyn_ResetAllControler(&w.hsynCtx, (unsigned)i);
    }
    sceHSyn_SetVoiceStatBuffer(&w.voiceStat);
    for (i = 15; i >= 0; i--) {
        w.stream[i].buffsize = 0x408;
        w.stream[i].validsize = 0;
    }

    for (i = 1; i >= 0; i--) {
        w.ads[i].buf = 0;
        w.ads[i].half = 0;
        w.ads[i].fd = -1;
        w.ads[i].state = ADS_CLOSED;
        w.ads[i].volR = 0x7FFF;
        w.ads[i].volL = 0x7FFF;
    }

    w.nextTick = port_ee_cycles() + TICK_EE_CYCLES;
    {
        /* KF4_AUDIO_FIRST_TICK=<EE cycle>: where the timer's ticks fall, for comparison with a
           reference whose timer started elsewhere (a development aid) */
        const char *first = getenv("KF4_AUDIO_FIRST_TICK");

        if (first != NULL && *first != 0) {
            uint64_t at = strtoull(first, NULL, 0);

            while (at <= port_ee_cycles()) {
                at += TICK_EE_CYCLES;
            }
            w.nextTick = port_ee_cycles() + (at - port_ee_cycles()) % TICK_EE_CYCLES;
            if (w.nextTick == port_ee_cycles()) {
                w.nextTick += TICK_EE_CYCLES;
            }
        }
    }
    w.initialised = 1;
    w.tickState = TICK_BLOCKED;
    w.adsThreadAlive = 0;
    w.adsDrainChannel = -1;
    w.adsSetActive = 0;
    w.adsSetThread = 0;
    return (int)SE_STREAM_ADDR;
}

void fsiopsnd_reset(void) {
    memset(&w, 0, sizeof(w));
    w.adsDrainChannel = -1;
    w.ads[0].fd = -1;
    w.ads[1].fd = -1;
}

int port_fsiopsnd_call(unsigned int function, void *send_buffer, int send_size, void *receive_buffer,
                       int receive_size) {
    uint64_t now = port_ee_cycles();
    const uint8_t *reply;
    int reply_size = 4;

    port_audio_sync(now);
    port_audio_lock();
    sd_set_time(now);
    memset(w.rpcArg, 0, sizeof(w.rpcArg));
    if (send_buffer != NULL && send_size > 0) {
        memcpy(w.rpcArg, send_buffer, (size_t)(send_size < (int)sizeof(w.rpcArg) ? send_size : (int)sizeof(w.rpcArg)));
    }
    sd_trace_mark("%llu %llu CALL 0x%x", (unsigned long long)port_vblank_count(), (unsigned long long)now,
                  function & 0xFFFF0);
    reply = fsSndrFunc(function, w.rpcArg);
    if (reply == (const uint8_t *)&w.voiceStat) {
        reply_size = sizeof(w.voiceStat);
    } else if (reply == w.chStat) {
        reply_size = sizeof(w.chStat);
    }
    if (receive_buffer != NULL) {
        /* a word even when the game asks for one byte: it reads the whole word back */
        int n = receive_size > 4 ? receive_size : 4;

        memset(receive_buffer, 0, (size_t)n);
        memcpy(receive_buffer, reply, (size_t)(n < reply_size ? n : reply_size));
    }
    port_audio_unlock();
    return 0;
}

/* Save states: the driver's state, the two modules' contexts and streams with it */
void port_fsiopsnd_state(PortStateIo *io) {
    PORT_STATE(io, w);
}
