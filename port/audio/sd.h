/* libsd, the IOP's SPU2 library, as the sound driver and modhsyn call it (port/audio/sd.c): the calls
   take libsd's encodings and go to the software SPU2 through ico-audio's front end (spu2_sd.h). */
#ifndef PORT_AUDIO_SD_H
#define PORT_AUDIO_SD_H

#include <stdint.h>

#include "spu2_sd.h"

/* libsd's register selectors (ps2sdk libsd-common.h): a voice parameter is core | voice << 1 | the
   parameter; core parameters set 0x80. */
#define SD_CORE_0 0
#define SD_CORE_1 1
#define SD_VOICE(core, v) ((core) | ((v) << 1))
#define SD_PARAM_MMIX SPU2_SD_PARAM_MMIX
#define SD_PARAM_MVOLL SPU2_SD_PARAM_MVOLL
#define SD_PARAM_MVOLR SPU2_SD_PARAM_MVOLR
#define SD_PARAM_EVOLL SPU2_SD_PARAM_EVOLL
#define SD_PARAM_EVOLR SPU2_SD_PARAM_EVOLR
#define SD_PARAM_AVOLL SPU2_SD_PARAM_AVOLL
#define SD_PARAM_AVOLR SPU2_SD_PARAM_AVOLR
#define SD_PARAM_BVOLL SPU2_SD_PARAM_BVOLL
#define SD_PARAM_BVOLR SPU2_SD_PARAM_BVOLR

/* sceSdBlockTrans modes */
#define SD_TRANS_WRITE 0
#define SD_TRANS_READ 1
#define SD_TRANS_STOP 2
#define SD_TRANS_LOOP 0x10

/* The EE time the calls that follow happen at (set by the driver at each RPC and tick); from inside
   the SPU2's render (a transfer callback) the calls happen at the frame being rendered. */
void sd_set_time(uint64_t ee_cycle);
void sd_set_time_render(void);
/* Which clock sd_set_time follows: the EE's (device 0; the SPU2 frame is EE cycles / 3072 + offset),
   or the sound device's (device 1: the calls happen at the frame the SPU2 is at) */
void sd_set_clock(int device, int64_t offset);

int sd_init(int flag);
void sd_set_param(uint16_t entry, uint16_t value);
uint16_t sd_get_param(uint16_t entry);
void sd_set_switch(uint16_t entry, uint32_t value);
uint32_t sd_get_switch(uint16_t entry);
void sd_set_addr(uint16_t entry, uint32_t value);
uint32_t sd_get_addr(uint16_t entry);
void sd_set_core_attr(uint16_t entry, uint16_t value);
uint16_t sd_get_core_attr(uint16_t entry);
int sd_set_effect_attr(int core, const spu2_sd_effect_attr *attr);
int sd_clear_effect_work_area(int core, int channel, int mode);

/* sceSdVoiceTrans: `iop` an IOP address, `spu` a byte address in sound memory. Returns the size,
   rounded up to 64 bytes as libsd's DMA moves it, or -1 while the channel is busy. */
int sd_voice_trans(int channel, uint16_t mode, uint32_t iop, uint32_t spu, uint32_t size);
int sd_voice_trans_status(int channel, int flag);

/* sceSdBlockTrans: the core's sound data input (AutoDMA) from an IOP ring of `size` bytes, in two
   halves; each finished half calls the channel's transfer callback. */
int sd_block_trans(int channel, uint16_t mode, uint32_t iop, uint32_t size);
/* sceSdBlockTransStatus: the half being transferred in bits 24 and up, and the address the transfer
   has reached in the low 24. */
uint32_t sd_block_trans_status(int channel, int flag);
typedef int (*sd_trans_callback)(void *data);
void sd_set_trans_callback(int channel, sd_trans_callback callback);

/* sceSdNote2Pitch: the PITCH word that plays `note` (fine in 1/128 semitones) of a sample whose
   unshifted pitch is centre_note/centre_fine */
uint16_t sd_note2pitch(uint16_t centre_note, uint16_t centre_fine, uint16_t note, int16_t fine);

/* Resets libsd's own state (the transfers and their callbacks) with the SPU2. */
void sd_reset(void);

/* The libsd trace (KF4_SD_TRACE=path): every call, in the order made, by the module making it
   (sd_trace_module: "FSIOPSND" or "MODHSYN"), for comparison with the PS2 driver's. sd_trace_mark
   writes a line of context (an RPC call, a tick). */
const char *sd_trace_module(const char *module);
void sd_trace_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
