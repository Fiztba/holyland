/* SDRDRV.IRX, the IOP end of libsdr: sceSdRemote runs libsd's calls on the IOP by number
   (port/include/port/audio.h). KF4 loads it for the movie player (gfx/movie.c), which plays a movie's
   sound through it: the PCM goes by SIF DMA into an IOP ring that core 0's sound data input plays
   (sceSdBlockTrans, looping), the player keeps its writes behind the input's address
   (sceSdBlockTransStatus), and core 1 mixes core 0's output in.

   The calls go to the same libsd as the sound driver's (port/audio/sd.c), at the EE time of the call,
   as port_fsiopsnd_call does. */
#include "port/audio.h"
#include "port/host.h"
#include "port/platform.h"
#include "sd.h"

/* libsdr's command numbers (rSdXxx) */
#define SDR_INIT 0x8000
#define SDR_SET_PARAM 0x8010
#define SDR_GET_PARAM 0x8020
#define SDR_SET_SWITCH 0x8030
#define SDR_GET_SWITCH 0x8040
#define SDR_SET_ADDR 0x8050
#define SDR_GET_ADDR 0x8060
#define SDR_SET_CORE_ATTR 0x8070
#define SDR_GET_CORE_ATTR 0x8080
#define SDR_VOICE_TRANS 0x80D0
#define SDR_BLOCK_TRANS 0x80E0
#define SDR_VOICE_TRANS_STATUS 0x80F0
#define SDR_BLOCK_TRANS_STATUS 0x8100

int port_sdrdrv_arg_count(int command) {
    switch (command) {
    case SDR_INIT:
    case SDR_GET_PARAM:
    case SDR_GET_SWITCH:
    case SDR_GET_ADDR:
    case SDR_GET_CORE_ATTR:
        return 1;
    case SDR_SET_PARAM:
    case SDR_SET_SWITCH:
    case SDR_SET_ADDR:
    case SDR_SET_CORE_ATTR:
    case SDR_VOICE_TRANS_STATUS:
    case SDR_BLOCK_TRANS_STATUS:
        return 2;
    case SDR_BLOCK_TRANS:
        return 4; /* and a fifth, the start address, for a transfer that starts mid-ring */
    case SDR_VOICE_TRANS:
        return 5;
    default:
        return -1;
    }
}

int port_sdrdrv_call(int command, const uint32_t *args) {
    uint64_t now = port_ee_cycles();
    const char *module;
    int result = 0;

    port_audio_sync(now);
    port_audio_lock();
    sd_set_time(now);
    module = sd_trace_module("SDRDRV");
    switch (command) {
    case SDR_INIT:
        result = sd_init((int)args[0]);
        break;
    case SDR_SET_PARAM:
        sd_set_param((uint16_t)args[0], (uint16_t)args[1]);
        break;
    case SDR_GET_PARAM:
        result = sd_get_param((uint16_t)args[0]);
        break;
    case SDR_SET_SWITCH:
        sd_set_switch((uint16_t)args[0], args[1]);
        break;
    case SDR_GET_SWITCH:
        result = (int)sd_get_switch((uint16_t)args[0]);
        break;
    case SDR_SET_ADDR:
        sd_set_addr((uint16_t)args[0], args[1]);
        break;
    case SDR_GET_ADDR:
        result = (int)sd_get_addr((uint16_t)args[0]);
        break;
    case SDR_SET_CORE_ATTR:
        sd_set_core_attr((uint16_t)args[0], (uint16_t)args[1]);
        break;
    case SDR_GET_CORE_ATTR:
        result = sd_get_core_attr((uint16_t)args[0]);
        break;
    case SDR_VOICE_TRANS:
        result = sd_voice_trans((int16_t)args[0], (uint16_t)args[1], args[2], args[3], args[4]);
        break;
    case SDR_VOICE_TRANS_STATUS:
        result = sd_voice_trans_status((int16_t)args[0], (int16_t)args[1]);
        break;
    case SDR_BLOCK_TRANS:
        if ((args[1] & 3) == 3) {
            PORT_TODO("sceSdRemote: a block transfer that starts mid-ring (the movie player's resume)");
        }
        result = sd_block_trans((int16_t)args[0], (uint16_t)args[1], args[2], args[3]);
        break;
    case SDR_BLOCK_TRANS_STATUS:
        result = (int)sd_block_trans_status((int16_t)args[0], (int16_t)args[1]);
        break;
    default:
        port_event("sceSdRemote: command 0x%X is not implemented", (unsigned)command);
        break;
    }
    sd_trace_module(module);
    port_audio_unlock();
    return result;
}
