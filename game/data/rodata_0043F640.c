/* rodata_0043F640.c: the game's data from the decomp's rodata_0043F640.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/load.h"
#include "sound/sound.h"

/* sound/sound.c */
int SoundDriver_VoiceLoadRelease(int *args);
int SoundDriver_VoiceLoadSendHeader(SoundLoadArgs *args, SoundReloadRequest *request);
int SoundDriver_VoiceLoadAllocBuffer(IopLoadRequest *request, IopLoadContext *context);
int SoundDriver_VoiceLoadSendPiece(SoundLoadArgs *args, SoundReloadRequest *request);
int SoundDriver_VoiceLoadWritePiece(SoundLoadArgs *args, SoundReloadRequest *request);
int SoundDriver_VoiceLoadBind(s32 unused, SoundReloadRequest *request);

/* 0x0043F640 */
char rpc_timeout_message[48] = "sceSifCheckStatRpc() == 1 in fssnd_Remote\n";

/* 0x0043F670 */
int (*const voice_load_steps[6])(int *, int *) = {
    (int (*)(int *, int *))SoundDriver_VoiceLoadRelease,
    (int (*)(int *, int *))SoundDriver_VoiceLoadSendHeader,
    (int (*)(int *, int *))SoundDriver_VoiceLoadAllocBuffer,
    (int (*)(int *, int *))SoundDriver_VoiceLoadSendPiece,
    (int (*)(int *, int *))SoundDriver_VoiceLoadWritePiece,
    (int (*)(int *, int *))SoundDriver_VoiceLoadBind,
};

/* 0x0043F688 */
char voice_load_waiting_message[16] = "waiting...\n";
