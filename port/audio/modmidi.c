/* modmidi, Sony's CSL MIDI sequencer (csl.h), as the disc's MODMIDI.IRX (PsIImodmidi 2200, library
   version 1.4) runs it: written from that module's code.

   A port plays one block of a .sq file (Vers, Sequ, then a Midi chunk of blocks, each with its own
   tick division and an optional table of compressed messages). Each ATick advances the port's clock
   by the tick interval and sends the channel messages that fall due to the output streams the
   channel is routed to (outPort), through the port's channel-message callback. Its event stream
   is not quite standard MIDI:
   - a note-off has one data byte;
   - a data byte with bit 7 set means no delta time follows the event;
   - An messages may stand for an entry of the block's compression table;
   - NRPN 0 / 1 (CC99, then CC6 = loop id, CC38 = repeat count, 0 = for ever) mark loops;
   - CC32 routes the channel to output stream (value & 15);
   - CC7 goes out scaled by the channel's and the port's volume.
   When play starts, the state each channel last set (program, controllers, bend) is sent again
   ("chase"); when it stops, the channels that sent anything get CC64 0 and CC123 0.

   KF4's driver sets only the channel-message callback: the meta, sysex and repeat callbacks stay
   NULL (Init clears them), so their call paths are left out. The Song API (chunks of song commands)
   is not used by KF4's driver and is not here either. */
#include <stddef.h>
#include <string.h>

#include "csl.h"
#include "port/platform.h"

static uint32_t tick_add; /* Init's interval << 7: the clock's advance per ATick, in us * 128 */
static int reported_bad_time;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

/* "SCEI" and a tag, each stored as a little-endian word (so "IECS" "sreV" on the disc) */
static int is_chunk(const uint8_t *p, const char *tag) {
    static const char sce[4] = {'S', 'C', 'E', 'I'};
    int i;

    for (i = 0; i < 4; i++) {
        if (p[i] != (uint8_t)sce[3 - i] || p[4 + i] != (uint8_t)tag[3 - i]) {
            return 0;
        }
    }
    return 1;
}

static MidiEnv *get_env(CslCtx *ctx, unsigned int port) {
    CslBuffGrp *group = &ctx->buffGrp[0];

    if (port * 2 + 1 >= (unsigned int)group->buffNum) {
        return NULL;
    }
    return group->buffCtx[port * 2 + 1].buff;
}

/* tickLen = ((tempo << 7) / division << 8) / relTempo, in unsigned 32 bits */
static void calc_tick_len(MidiEnv *e) {
    if (e->division == 0 || e->rel_tempo == 0) {
        /* a division by zero, which stops the IOP */
        if (!reported_bad_time) {
            reported_bad_time = 1;
            port_event("modmidi: a sequence with division %u, relative tempo %u (the IOP would stop)",
                       (unsigned)e->division, (unsigned)e->rel_tempo);
        }
        e->tick_len = UINT32_MAX;
        return;
    }
    e->tick_len = (((e->tempo << 7) / e->division) << 8) / e->rel_tempo;
}

static uint32_t read_vlq(MidiEnv *e) {
    uint32_t v = 0;
    uint8_t b;

    do {
        b = *e->ptr++;
        v = (v << 7) + (b & 0x7F);
    } while (b & 0x80);
    return v;
}

/* The delta time after an event, absent when the event's last data byte had bit 7 set */
static void read_delta(MidiEnv *e) {
    if (e->no_delta) {
        e->no_delta = 0;
        return;
    }
    e->next_time += read_vlq(e);
}

static uint8_t scaled_volume(MidiEnv *e, int ch) {
    uint32_t v = (uint32_t)(e->cc7[ch] * e->ch_vol[ch] * e->master_vol) >> 14;

    return v >= 0x80 ? 0x7F : (uint8_t)v;
}

/* A channel message (status | data1 << 8 | data2 << 16, len 2 or 3 bytes) to the streams the
   channel goes to; out is NULL while seeking (the callback still sees it) */
static void send_message(MidiEnv *e, CslBuffGrp *out, uint32_t msg, int len) {
    uint16_t mask;
    uint32_t i;

    msg &= 0x7F7FFF;
    if (e->chMsgCallBack != NULL) {
        msg = e->chMsgCallBack(msg, e->chMsgCallBackPrivateData);
        if (msg == 0) {
            return;
        }
    }
    e->used_ch |= (uint16_t)(1u << (msg & 0xF));
    if (out == NULL) {
        return;
    }
    mask = e->outPort[msg & 0xF];
    for (i = 0; mask != 0; i++, mask >>= 1) {
        CslMidiStream *stream;
        uint32_t valid;
        uint32_t m = msg;
        int k;

        if (!(mask & 1) || i >= (uint32_t)out->buffNum) {
            continue;
        }
        stream = out->buffCtx[i].buff;
        valid = stream->validsize;
        if (stream->buffsize < valid + (uint32_t)len + 8) {
            continue; /* "sendChMsg Buffer OverRun" */
        }
        stream->validsize = valid + (uint32_t)len;
        for (k = 0; k < len; k++) {
            stream->data[valid + (uint32_t)k] = (uint8_t)m;
            m >>= 8;
        }
    }
}

/* On stop or at the end: hold off and all notes off on each channel that sent anything */
static void stop_notes(MidiEnv *e, CslBuffGrp *out) {
    int ch;

    for (ch = 0; ch < 16; ch++) {
        if (e->used_ch & (1u << ch)) {
            send_message(e, out, 0x0040B0u | (uint32_t)ch, 3);
            send_message(e, out, 0x007BB0u | (uint32_t)ch, 3);
        }
    }
    e->used_ch = 0;
}

static void resend_volume(MidiEnv *e, CslBuffGrp *out, int ch) {
    if (e->status & MIDI_ST_INPLAY) {
        send_message(e, out, (uint32_t)ch | 0x7B0u | (uint32_t)scaled_volume(e, ch) << 16, 3);
    }
}

/* When play starts: what each channel set, again */
static void chase(MidiEnv *e, CslBuffGrp *out) {
    uint32_t ch;

    for (ch = 0; ch < 16; ch++) {
        MidiChase *c = &e->chase[ch];

        if (!(c->cc0 & 0x80)) {
            send_message(e, out, ch | 0x00B0u | (uint32_t)c->cc0 << 16, 3);
        }
        if (!(c->prog & 0x80)) {
            send_message(e, out, ch | 0xC0u | (uint32_t)c->prog << 8, 2);
        }
        if (!(c->cc1 & 0x80)) {
            send_message(e, out, ch | 0x01B0u | (uint32_t)c->cc1 << 16, 3);
        }
        if (!(c->cc2 & 0x80)) {
            send_message(e, out, ch | 0x02B0u | (uint32_t)c->cc2 << 16, 3);
        }
        if (!(c->cc5 & 0x80)) {
            send_message(e, out, ch | 0x05B0u | (uint32_t)c->cc5 << 16, 3);
        }
        if (!(c->cc7 & 0x80)) {
            e->cc7[ch] = c->cc7;
            resend_volume(e, out, (int)ch);
        }
        if (!(c->cc10 & 0x80)) {
            send_message(e, out, ch | 0x0AB0u | (uint32_t)c->cc10 << 16, 3);
        }
        if (!(c->cc11 & 0x80)) {
            send_message(e, out, ch | 0x0BB0u | (uint32_t)c->cc11 << 16, 3);
        }
        if (!(c->cc64 & 0x80)) {
            send_message(e, out, ch | 0x40B0u | (uint32_t)c->cc64 << 16, 3);
        }
        if (!(c->cc65 & 0x80)) {
            send_message(e, out, ch | 0x41B0u | (uint32_t)c->cc65 << 16, 3);
        }
        if (!(c->bend & 0x8080)) {
            send_message(e, out, ch | 0xE0u | (uint32_t)c->bend << 8, 3);
        }
    }
}

static MidiLoop *find_loop(MidiEnv *e, uint8_t id) {
    int i;

    for (i = 0; i < 8; i++) {
        if (e->loop[i].id == id) {
            return &e->loop[i];
        }
    }
    return NULL;
}

/* NRPN loops: CC99 0 then CC6 id starts loop id; CC99 1, CC6 id, CC38 n ends it (n repeats, 0 for
   ever). Not taken while seeking or with MIDI_ST_NOLOOP. */
static void loop_control(MidiEnv *e, uint8_t cc, uint8_t value, CslBuffGrp *out) {
    MidiLoop *l;
    int i;

    switch (cc) {
    case 0x63:
        e->nrpn_msb = value;
        e->data_msb = 0xFF;
        break;
    case 0x06:
        e->data_msb = value;
        if (e->nrpn_msb != 0) {
            break;
        }
        l = find_loop(e, value);
        for (i = 0; i < 8 && l == NULL; i++) {
            if (e->loop[i].id == 0xFF) {
                l = &e->loop[i];
            }
        }
        if (l == NULL) {
            break; /* "loop table overflow" */
        }
        l->id = value;
        l->count = 0xFF;
        l->run_status = e->run_status;
        l->no_delta = e->no_delta;
        l->time = e->next_time;
        l->ptr = e->ptr;
        break;
    case 0x26:
        if (e->nrpn_msb != 1 || e->data_msb == 0xFF || (e->status & MIDI_ST_NOLOOP) || out == NULL) {
            break;
        }
        l = find_loop(e, e->data_msb);
        if (l == NULL) {
            break; /* "not found loop start" */
        }
        if (l->count == 0xFF) {
            l->count = value;
        }
        if (value != 0 && l->count == 0) {
            l->count = 0xFF; /* done: play on past the end */
            break;
        }
        e->ptr = l->ptr;
        e->position = e->next_time = l->time;
        e->no_delta = l->no_delta;
        e->run_status = l->run_status;
        if (value != 0) {
            l->count--;
        }
        break;
    default:
        break;
    }
}

/* A meta event: 0 ends play (end of track, or a tempo event of the wrong length) */
static int meta_event(MidiEnv *e, uint8_t type) {
    uint32_t len = read_vlq(e);

    if (type == 0x2F) {
        e->status |= MIDI_ST_DATAEND;
        return 0;
    }
    if (type == 0x51) {
        if (len != 3) {
            return 0; /* "parseMetaEvent tempo length error" */
        }
        e->tempo = (uint32_t)e->ptr[0] << 16 | (uint32_t)e->ptr[1] << 8 | e->ptr[2];
        e->ptr += 3;
        calc_tick_len(e);
        return 1;
    }
    e->ptr += len;
    return 1;
}

/* A sysex: F0, length, data, to the streams of excOutPort. The module reads the length from the byte
   after the one it already took as the first data byte. */
static void sysex_event(MidiEnv *e, CslBuffGrp *out) {
    uint32_t len = read_vlq(e);
    uint16_t mask = e->excOutPort;
    uint32_t i;

    for (i = 0; out != NULL && mask != 0; i++, mask >>= 1) {
        CslMidiStream *stream;
        uint32_t valid;

        if (!(mask & 1) || i >= (uint32_t)out->buffNum) {
            continue;
        }
        stream = out->buffCtx[i].buff;
        valid = stream->validsize;
        if (stream->buffsize < valid + len + 9) {
            continue; /* "paesExcEvent Buffer OverRun" */
        }
        stream->validsize = valid + 1 + len;
        stream->data[valid] = 0xF0;
        memcpy(stream->data + valid + 1, e->ptr, len);
    }
    e->ptr += len;
}

/* Runs every event due at the port's position: 1 to play on, 0 at the end or on an error */
static int play_events(MidiEnv *e, CslBuffGrp *out) {
    while (e->position >= e->next_time) {
        const uint8_t *p = e->ptr;
        uint8_t status = *p++;
        uint8_t d1;

        if (status & 0x80) {
            d1 = *p++;
        } else {
            if (e->run_status == 0) {
                e->ptr = p;
                return 0; /* "playEnv running status error" */
            }
            d1 = status;
            status = e->run_status;
        }
        e->ptr = p;

        if (status < 0xF0) {
            uint32_t msg = status | (uint32_t)d1 << 8;
            uint8_t d2 = 0;
            int len = 3;
            int ch = status & 0xF;

            e->run_status = status;
            switch (status & 0xF0) {
            case 0x80:
                len = 2;
                break;
            case 0xC0:
                e->chase[ch].prog = d1 & 0x7F;
                len = 2;
                break;
            case 0xD0:
                len = 2;
                break;
            case 0x90:
            case 0xE0:
                d2 = *e->ptr++;
                msg |= (uint32_t)d2 << 16;
                if (status >= 0xE0) {
                    e->chase[ch].bend = (uint16_t)(msg >> 8);
                }
                break;
            case 0xA0:
                if (e->comp_table != NULL) {
                    /* a compressed message: d1 = table index << 4 | a 4-bit value */
                    uint32_t index = (uint32_t)(ch | (d1 & 0xF0)) * 2;

                    if (index < rd16(e->comp_table + 2)) {
                        msg = e->comp_table[4 + index] | (uint32_t)e->comp_table[5 + index] << 8 |
                              (uint32_t)(((d1 & 0xF) << 3) | 7) << 16;
                    } else {
                        len = 0;
                    }
                } else {
                    d2 = *e->ptr++;
                    msg |= (uint32_t)d2 << 16;
                }
                break;
            case 0xB0:
                d2 = *e->ptr++;
                msg |= (uint32_t)d2 << 16;
                switch (d1) {
                case 0x00:
                    e->chase[ch].cc0 = d2 & 0x7F;
                    break;
                case 0x01:
                    e->chase[ch].cc1 = d2 & 0x7F;
                    break;
                case 0x02:
                    e->chase[ch].cc2 = d2 & 0x7F;
                    break;
                case 0x05:
                    e->chase[ch].cc5 = d2 & 0x7F;
                    break;
                case 0x07:
                    e->cc7[ch] = e->chase[ch].cc7 = d2 & 0x7F;
                    d2 = (uint8_t)((d2 & 0x80) | scaled_volume(e, ch));
                    msg = (msg & 0xFFFF) | (uint32_t)d2 << 16;
                    break;
                case 0x0A:
                    e->chase[ch].cc10 = d2 & 0x7F;
                    break;
                case 0x0B:
                    e->chase[ch].cc11 = d2 & 0x7F;
                    break;
                case 0x40:
                    e->chase[ch].cc64 = d2 & 0x7F;
                    break;
                case 0x41:
                    e->chase[ch].cc65 = d2 & 0x7F;
                    break;
                case 0x20:
                    e->outPort[ch] = (uint16_t)(1u << (d2 & 0xF));
                    break;
                case 0x06:
                case 0x26:
                case 0x62:
                case 0x63:
                    loop_control(e, d1, d2 & 0x7F, out);
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
            if (len != 0) {
                send_message(e, out, msg, len);
            }
            e->no_delta = (d1 | d2) & 0x80;
        } else {
            e->no_delta = 0;
            if (status == 0xFF) {
                if (!meta_event(e, d1)) {
                    return 0;
                }
            } else if (status == 0xF0) {
                sysex_event(e, out);
            } else if (status == 0xF7) {
                uint32_t n;

                e->ptr--;
                n = read_vlq(e);
                e->ptr += n;
            } else {
                return 0; /* "playEnv unknown status" */
            }
        }
        read_delta(e);
    }
    return 1;
}

/* Back to the start of the selected block */
static void rewind_block(MidiEnv *e) {
    int ch;

    e->position = 0;
    e->next_time = 0;
    e->acc = 0;
    e->tick_len = 0;
    e->run_status = 0;
    e->no_delta = 0;
    e->data_msb = 0xFF;
    e->nrpn_msb = 0xFF;
    e->ptr = e->data_start;
    for (ch = 0; ch < 16; ch++) {
        MidiChase *c = &e->chase[ch];

        e->cc7[ch] = 0x7F;
        c->prog = c->cc0 = c->cc1 = c->cc2 = c->cc5 = c->cc7 = 0xFF;
        c->cc10 = c->cc11 = c->cc64 = c->cc65 = 0xFF;
        c->bend = 0xFFFF;
    }
    calc_tick_len(e);
    read_delta(e);
}

/* ---- The API ------------------------------------------------------------------------------------- */
int sceMidi_Load(CslCtx *ctx, unsigned int port) {
    CslBuffGrp *group = &ctx->buffGrp[0];
    MidiEnv *e;
    const uint8_t *sq;
    const uint8_t *header;
    uint32_t song;
    uint32_t midi;
    int i;

    if (port * 2 + 1 >= (unsigned int)group->buffNum) {
        return -1;
    }
    e = group->buffCtx[port * 2 + 1].buff;
    e->songNum = UINT32_MAX;
    e->midiNum = UINT32_MAX;
    e->position = 0;
    e->status = 0;
    e->sequ_chunk = NULL;
    e->midi_chunk = NULL;
    e->song_chunk = NULL;
    e->used_ch = 0;
    for (i = 0; i < 16; i++) {
        e->ch_vol[i] = 0x80;
    }
    e->master_vol = 0x80;
    sq = group->buffCtx[port * 2].buff;
    if (sq == NULL || !is_chunk(sq, "Vers")) {
        return -1;
    }
    header = sq + rd32(sq + 8);
    if (!is_chunk(header, "Sequ")) {
        return -1;
    }
    song = rd32(header + 0x10);
    midi = rd32(header + 0x14);
    if (midi == UINT32_MAX || !is_chunk(sq + midi, "Midi")) {
        return -1;
    }
    e->sequ_chunk = header;
    e->midi_chunk = sq + midi;
    e->song_chunk = (song == UINT32_MAX || !is_chunk(sq + song, "Song")) ? NULL : sq + song;
    e->status = MIDI_ST_READY;
    return 0;
}

int sceMidi_Init(CslCtx *ctx, unsigned int interval_us) {
    CslBuffGrp *group;
    CslBuffGrp *out;
    unsigned int port;
    int i;

    tick_add = interval_us << 7;
    if (ctx == NULL || ctx->buffGrpNum < 2 || ctx->buffGrp == NULL) {
        return -1;
    }
    group = &ctx->buffGrp[0];
    if (group->buffNum < 2 || group->buffCtx == NULL) {
        return -1;
    }
    for (port = 0; port * 2 + 1 < (unsigned int)group->buffNum; port++) {
        MidiEnv *e = group->buffCtx[port * 2 + 1].buff;

        if (e == NULL) {
            return -1;
        }
        for (i = 0; i < 16; i++) {
            e->outPort[i] = 1;
        }
        e->excOutPort = 1;
        e->chMsgCallBack = NULL;
        e->metaMsgCallBack = NULL;
        e->excMsgCallBack = NULL;
        e->repeatCallBack = NULL;
        sceMidi_Load(ctx, port);
    }
    out = &ctx->buffGrp[1];
    if (out->buffNum == 0 || out->buffCtx == NULL) {
        return -1;
    }
    for (i = 0; i < out->buffNum; i++) {
        CslMidiStream *stream = out->buffCtx[i].buff;

        if (stream == NULL) {
            return -1;
        }
        stream->validsize = 0;
    }
    return 0;
}

int sceMidi_ATick(CslCtx *ctx) {
    CslBuffGrp *group = &ctx->buffGrp[0];
    int i;

    for (i = 1; i < group->buffNum; i += 2) {
        MidiEnv *e = group->buffCtx[i].buff;

        if (e->status & MIDI_ST_INPLAY) {
            CslBuffGrp *out = &ctx->buffGrp[1];
            uint32_t acc = e->acc + tick_add;

            for (;;) {
                if (!play_events(e, out)) {
                    acc = 0;
                    e->status &= ~MIDI_ST_INPLAY;
                    stop_notes(e, out);
                    break;
                }
                if (acc < e->tick_len) {
                    break;
                }
                if (e->tick_len == 0) {
                    /* the IOP would loop here for ever */
                    port_event("modmidi: port %d has a tick length of 0", i / 2);
                    break;
                }
                acc -= e->tick_len;
                e->position++;
            }
            e->acc = acc;
        }
    }
    return 0;
}

int sceMidi_SelectMidi(CslCtx *ctx, unsigned int port, unsigned int midi_num) {
    MidiEnv *e = get_env(ctx, port);
    const uint8_t *chunk;
    const uint8_t *block;
    int i;

    if (e == NULL) {
        return -1;
    }
    chunk = e->midi_chunk;
    if (e->status & MIDI_ST_INPLAY) {
        sceMidi_MidiPlaySwitch(ctx, port, 0);
    }
    e->status &= ~(MIDI_ST_INPLAY | MIDI_ST_DATAEND);
    e->midiNum = UINT32_MAX;
    if (!(e->status & MIDI_ST_READY) || chunk == NULL || rd32(chunk + 0x0C) < midi_num) {
        return -1;
    }
    block = chunk + rd32(chunk + 0x10 + midi_num * 4);
    e->comp_table = block + 6;
    if (rd16(block + 6) != 1 || rd16(block + 8) == 0 || rd32(block) < 0x0C) {
        e->comp_table = NULL;
    }
    for (i = 0; i < 8; i++) {
        e->loop[i].id = 0xFF;
        e->loop[i].count = 0xFF;
    }
    e->data_start = block + rd32(block);
    e->tempo = 500000;
    e->rel_tempo = 0x100;
    e->division = rd16(block + 4);
    rewind_block(e);
    e->midiNum = midi_num;
    return 0;
}

int sceMidi_MidiPlaySwitch(CslCtx *ctx, unsigned int port, int sw) {
    MidiEnv *e = get_env(ctx, port);
    CslBuffGrp *out;

    if (e == NULL) {
        return -1;
    }
    out = &ctx->buffGrp[1];
    if (sw == 0) {
        if (e->status & MIDI_ST_INPLAY) {
            stop_notes(e, out);
            e->status &= ~MIDI_ST_INPLAY;
        }
        return 0;
    }
    if (sw != 1) {
        return 0;
    }
    if ((e->status & (MIDI_ST_READY | MIDI_ST_DATAEND)) != MIDI_ST_READY || e->midiNum == UINT32_MAX) {
        return -1;
    }
    chase(e, out);
    e->status |= MIDI_ST_INPLAY;
    return 0;
}

/* Seeks: the events up to the tick run with no output (the callback still sees them, loops are not
   taken), and play then starts from the state they left */
int sceMidi_MidiSetLocation(CslCtx *ctx, unsigned int port, unsigned int tick) {
    MidiEnv *e = get_env(ctx, port);

    if (e == NULL) {
        return -1;
    }
    if (tick < e->position) {
        rewind_block(e);
    }
    while (e->position < tick) {
        if (e->status & MIDI_ST_DATAEND) {
            return -1;
        }
        if (e->next_time >= tick) {
            e->position = tick;
            break;
        }
        e->position = e->next_time;
        if (!play_events(e, NULL)) {
            break;
        }
    }
    return e->position == tick ? 0 : -1;
}

unsigned char sceMidi_MidiGetAbsoluteTempo(CslCtx *ctx, unsigned int port) {
    MidiEnv *e = get_env(ctx, port);
    uint32_t bpm;

    if (e == NULL || e->midiNum == UINT32_MAX || e->tempo == 0) {
        return 0;
    }
    bpm = 60000000u / e->tempo;
    if (bpm < 20) {
        bpm = 20;
    }
    if (bpm > 255) {
        bpm = 255;
    }
    return (unsigned char)bpm;
}

int sceMidi_MidiSetRelativeTempo(CslCtx *ctx, unsigned int port, unsigned short rel) {
    MidiEnv *e = get_env(ctx, port);

    if (e == NULL) {
        return -1;
    }
    e->rel_tempo = rel;
    calc_tick_len(e);
    return 0;
}

unsigned short sceMidi_MidiGetRelativeTempo(CslCtx *ctx, unsigned int port) {
    MidiEnv *e = get_env(ctx, port);

    if (e == NULL || e->midiNum == UINT32_MAX) {
        return 0;
    }
    return (unsigned short)e->rel_tempo;
}
