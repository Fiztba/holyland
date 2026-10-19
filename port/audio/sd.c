/* libsd for the sound driver and modhsyn (sd.h): the calls go to ico-audio's front end, stamped
   with the EE time they happen at.

   The sound data input (sceSdBlockTrans in loop mode). The SPU2 reads its input from a ring of two
   0x400-byte halves (spu2_memin_*); libsd's DMA keeps that ring filled from the caller's IOP ring,
   which is larger (FSIOPSND's is 0x10000 bytes), and calls the transfer callback each time it has
   read one half of the IOP ring. Here the SPU2's half-done callback copies the next 0x400 bytes of
   the IOP ring into the half it finished, and when that read completes a half of the IOP ring, the
   transfer callback runs, as libsd's interrupt handler does. Both rings hold 0x200 bytes of left
   samples then 0x200 of right ones, in turn. */
#include "sd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/audio.h"
#include "spu2.h"
#include "spu2_internal.h"

#define MEMIN_HALF 0x400u

/* ---- The trace ---------------------------------------------------------------------------------- */
static FILE *trace;
static int trace_checked;
static const char *trace_module = "FSIOPSND";

static FILE *trace_file(void) {
    if (!trace_checked) {
        const char *path = getenv("KF4_SD_TRACE");

        trace_checked = 1;
        if (path != NULL && *path != '\0') {
            trace = fopen(path, "w");
        }
    }
    return trace;
}

const char *sd_trace_module(const char *module) {
    const char *previous = trace_module;

    trace_module = module;
    return previous;
}

void sd_trace_mark(const char *fmt, ...) {
    va_list args;

    if (trace_file() == NULL) {
        return;
    }
    va_start(args, fmt);
    vfprintf(trace, fmt, args);
    va_end(args);
    fputc('\n', trace);
}

static void trace_call(uint32_t result, const char *name, int count, ...) {
    va_list args;
    int i;

    if (trace_file() == NULL) {
        return;
    }
    fprintf(trace, "  SD %s %s(", trace_module, name);
    va_start(args, count);
    for (i = 0; i < count; i++) {
        fprintf(trace, i ? ", 0x%x" : "0x%x", va_arg(args, unsigned int));
    }
    va_end(args);
    fprintf(trace, ") = 0x%x\n", result);
}

#define TRACE(result, name, ...) trace_call((uint32_t)(result), name, (int)(sizeof((unsigned int[]){__VA_ARGS__}) / sizeof(unsigned int)), __VA_ARGS__)

static uint64_t ee_frame(uint64_t ee_cycle) {
    return ee_cycle / AUDIO_EE_CYCLES_PER_FRAME;
}

static int clock_device;
static int64_t clock_offset;

void sd_set_clock(int device, int64_t offset) {
    clock_device = device;
    clock_offset = offset;
}

void sd_set_time(uint64_t ee_cycle) {
    if (clock_device) {
        spu2_sd_set_time(spu2_time());
        return;
    }
    spu2_sd_set_time((uint64_t)((int64_t)ee_frame(ee_cycle) + clock_offset));
}

void sd_set_time_render(void) {
    spu2_sd_set_time(spu2_time());
}

int sd_init(int flag) {
    TRACE(0, "sceSdInit", (unsigned)flag);
    spu2_sd_init(flag);
    return 0;
}

void sd_set_param(uint16_t entry, uint16_t value) {
    TRACE(0, "sceSdSetParam", entry, value);
    spu2_sd_set_param(entry, value);
}

uint16_t sd_get_param(uint16_t entry) {
    uint16_t value = spu2_sd_get_param(entry);

    TRACE(value, "sceSdGetParam", entry);
    return value;
}

void sd_set_switch(uint16_t entry, uint32_t value) {
    TRACE(0, "sceSdSetSwitch", entry, value);
    spu2_sd_set_switch(entry, value);
}

uint32_t sd_get_switch(uint16_t entry) {
    uint32_t value = spu2_sd_get_switch(entry);

    TRACE(value, "sceSdGetSwitch", entry);
    return value;
}

void sd_set_addr(uint16_t entry, uint32_t value) {
    TRACE(0, "sceSdSetAddr", entry, value);
    spu2_sd_set_addr(entry, value);
}

uint32_t sd_get_addr(uint16_t entry) {
    uint32_t value = spu2_sd_get_addr(entry);

    TRACE(value, "sceSdGetAddr", entry);
    return value;
}

void sd_set_core_attr(uint16_t entry, uint16_t value) {
    TRACE(0, "sceSdSetCoreAttr", entry, value);
    spu2_sd_set_core_attr(entry, value);
}

uint16_t sd_get_core_attr(uint16_t entry) {
    uint16_t value = spu2_sd_get_core_attr(entry);

    TRACE(value, "sceSdGetCoreAttr", entry);
    return value;
}

int sd_set_effect_attr(int core, const spu2_sd_effect_attr *attr) {
    int result = spu2_sd_set_effect_attr(core, attr);

    TRACE(result, "sceSdSetEffectAttr", (unsigned)core, (unsigned)attr->mode, (unsigned)(uint16_t)attr->depth_L,
          (unsigned)(uint16_t)attr->depth_R, (unsigned)attr->delay, (unsigned)attr->feedback);
    return result;
}

/* sceSdNote2Pitch, as the disc's LIBSD.IRX computes it: 0x8000 * 2^(i/12) and 0x8000 * 2^(i/1536)
   from two tables, two octaves below the result, shifted down with rounding to the octave */
static const uint16_t note2pitch_semitone[12] = {
    0x8000, 0x879C, 0x8FAC, 0x9837, 0xA145, 0xAADC, 0xB504, 0xBFC8, 0xCB2F, 0xD744, 0xE411, 0xF1A1,
};
static const uint16_t note2pitch_fine[128] = {
    0x8000, 0x800E, 0x801D, 0x802C, 0x803B, 0x804A, 0x8058, 0x8067, 0x8076, 0x8085, 0x8094, 0x80A3,
    0x80B1, 0x80C0, 0x80CF, 0x80DE, 0x80ED, 0x80FC, 0x810B, 0x811A, 0x8129, 0x8138, 0x8146, 0x8155,
    0x8164, 0x8173, 0x8182, 0x8191, 0x81A0, 0x81AF, 0x81BE, 0x81CD, 0x81DC, 0x81EB, 0x81FA, 0x8209,
    0x8218, 0x8227, 0x8236, 0x8245, 0x8254, 0x8263, 0x8272, 0x8282, 0x8291, 0x82A0, 0x82AF, 0x82BE,
    0x82CD, 0x82DC, 0x82EB, 0x82FA, 0x830A, 0x8319, 0x8328, 0x8337, 0x8346, 0x8355, 0x8364, 0x8374,
    0x8383, 0x8392, 0x83A1, 0x83B0, 0x83C0, 0x83CF, 0x83DE, 0x83ED, 0x83FD, 0x840C, 0x841B, 0x842A,
    0x843A, 0x8449, 0x8458, 0x8468, 0x8477, 0x8486, 0x8495, 0x84A5, 0x84B4, 0x84C3, 0x84D3, 0x84E2,
    0x84F1, 0x8501, 0x8510, 0x8520, 0x852F, 0x853E, 0x854E, 0x855D, 0x856D, 0x857C, 0x858B, 0x859B,
    0x85AA, 0x85BA, 0x85C9, 0x85D9, 0x85E8, 0x85F8, 0x8607, 0x8617, 0x8626, 0x8636, 0x8645, 0x8655,
    0x8664, 0x8674, 0x8683, 0x8693, 0x86A2, 0x86B2, 0x86C1, 0x86D1, 0x86E0, 0x86F0, 0x8700, 0x870F,
    0x871F, 0x872E, 0x873E, 0x874E, 0x875D, 0x876D, 0x877D, 0x878C,
};

uint16_t sd_note2pitch(uint16_t centre_note, uint16_t centre_fine, uint16_t note, int16_t fine) {
    int32_t f = fine + centre_fine;
    int32_t fq = (f < 0 ? f + 127 : f) >> 7;
    int32_t n = (int16_t)(note + fq - centre_note);
    int32_t fr = f - (fq << 7);
    int32_t q = n / 12;
    int32_t octave = q - 2;
    int32_t index = n - q * 12;
    int32_t r;

    if ((int16_t)index < 0 || (index == 0 && fr < 0)) {
        index += 12;
        octave = q - 3;
    }
    if (fr < 0) {
        index = index - 1 + fq;
        fr += (fq + 1) << 7;
    }
    r = ((int32_t)note2pitch_semitone[(int16_t)index] * note2pitch_fine[fr]) >> 16;
    if ((int16_t)octave < 0) {
        int shift = -(int16_t)octave;

        r = (int32_t)(((uint32_t)r + (1u << (shift - 1))) >> shift);
    }
    TRACE((uint16_t)r, "sceSdNote2Pitch", centre_note, centre_fine, note, (unsigned)(uint16_t)fine);
    return (uint16_t)r;
}

/* sceSdClearEffectWorkArea: zeroes the work area of effect mode `mode` below the core's end
   address, by DMA on `channel` (done at once here). */
int sd_clear_effect_work_area(int core, int channel, int mode) {
    static const uint8_t zero[1024];
    const spu2_reverb_preset *preset;
    uint32_t end;
    uint32_t start;
    uint32_t done;

    TRACE(0, "sceSdClearEffectWorkArea", (unsigned)core, (unsigned)channel, (unsigned)mode);
    (void)channel;
    core &= 1;
    if (mode < 0 || mode > 9) {
        return -1;
    }
    preset = spu2_reverb_get_preset(mode);
    if (preset == NULL || preset->size == 0) {
        return 0;
    }
    end = ((uint32_t)(spu2_shadow(core, SPU2_R_EEA) & 0xF) << 17) | 0x1FFFFu;
    start = end + 1u - preset->size;
    for (done = 0; done < preset->size; done += sizeof zero) {
        uint32_t n = preset->size - done < sizeof zero ? preset->size - done : (uint32_t)sizeof zero;

        spu2_dma_write(start + done, zero, n);
    }
    return 0;
}

/* ---- Voice transfers ------------------------------------------------------------------------- */
int sd_voice_trans(int channel, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size) {
    uint32_t rounded = (size + 63u) & ~63u;
    int result;

    channel &= 1;
    if (iop + rounded > IOP_RAM_SIZE) {
        rounded = IOP_RAM_SIZE - iop;
    }
    result = spu2_sd_voice_trans(channel, mode, port_iop_ptr(iop), spu, rounded);
    TRACE(result < 0 ? (uint32_t)result : rounded, "sceSdVoiceTrans", (unsigned)channel, mode, iop, spu, size);
    return result < 0 ? result : (int)rounded;
}

/* flag 0 polls, 1 waits. A poll of a busy channel finishes the transfer: the game would otherwise
   spin on it at one EE time, which does not move while it does (the IOP's DMA would have finished
   it in well under a frame). */
int sd_voice_trans_status(int channel, int flag) {
    channel &= 1;
    if (spu2_voice_trans_busy(channel)) {
        spu2_voice_trans_wait(channel);
    }
    TRACE(1, "sceSdVoiceTransStatus", (unsigned)channel, (unsigned)flag);
    return 1;
}

/* ---- The sound data input --------------------------------------------------------------------- */
typedef struct {
    int running;
    uint32_t ring;           /* the IOP ring's address and size */
    uint32_t size;
    uint32_t read;           /* the next byte of the IOP ring the DMA reads */
    int half;                /* the half of the IOP ring being read (libsd's BlockTransBuff) */
    uint8_t memin[2 * MEMIN_HALF];
    sd_trans_callback callback;
} AutoDma;

static AutoDma auto_dma[2];

/* Reads the next 0x400 bytes of the IOP ring into a half of the SPU2's ring. */
static void dma_fill(AutoDma *dma, int memin_half) {
    memcpy(dma->memin + memin_half * MEMIN_HALF, port_iop_ptr(dma->ring + dma->read), MEMIN_HALF);
    dma->read += MEMIN_HALF;
    if (dma->read % (dma->size / 2) == 0) {
        dma->half ^= 1;
        if (dma->read == dma->size) {
            dma->read = 0;
        }
        if (dma->callback != NULL) {
            sd_set_time_render();
            dma->callback(NULL);
        }
    }
}

static void memin_done(int core, int half, void *user) {
    AutoDma *dma = &auto_dma[core];

    (void)user;
    if (dma->running) {
        dma_fill(dma, half);
    }
}

int sd_block_trans(int channel, uint16_t mode, uint32_t iop, uint32_t size) {
    AutoDma *dma = &auto_dma[channel & 1];
    int core = channel & 1;

    TRACE(0, "sceSdBlockTrans", (unsigned)channel, mode, iop, size);
    if ((mode & 3) == SD_TRANS_STOP) {
        dma->running = 0;
        spu2_memin_stop(core);
        return 0;
    }
    if ((mode & 3) != SD_TRANS_WRITE || !(mode & SD_TRANS_LOOP) || size < 2 * MEMIN_HALF ||
        size % (2 * MEMIN_HALF) != 0) {
        /* one-shot and read transfers: KF4's driver makes none */
        return -1;
    }
    dma->ring = iop;
    dma->size = size;
    dma->read = 0;
    dma->half = 0;
    dma->running = 1;
    dma_fill(dma, 0);
    dma_fill(dma, 1);
    spu2_memin_start(core, dma->memin, memin_done, NULL);
    return 0;
}

uint32_t sd_block_trans_status(int channel, int flag) {
    AutoDma *dma = &auto_dma[channel & 1];

    uint32_t status = 0;

    if (dma->running) {
        /* the IOP DMA's MADR: the next byte of the ring it reads (the movie player keeps its writes
           behind it) */
        status = ((uint32_t)dma->half << 24) | ((dma->ring + dma->read) & 0xFFFFFFu);
    }
    TRACE(status, "sceSdBlockTransStatus", (unsigned)channel, (unsigned)flag);
    return status;
}

void sd_set_trans_callback(int channel, sd_trans_callback callback) {
    TRACE(0, "sceSdSetTransCallback", (unsigned)channel, callback != NULL);
    auto_dma[channel & 1].callback = callback;
}

void sd_reset(void) {
    memset(auto_dma, 0, sizeof(auto_dma));
}
