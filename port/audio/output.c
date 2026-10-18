/* The SPU2's output (port/include/port/audio.h): rendered on EE time and handed to the outputs.

   The SPU2 renders up to the current EE time at each vblank, at each of the driver's timer ticks and
   before each call to the driver, so what the driver reads back (a stream's state, the half the input
   is on) is as up to date as it was on the PS2. The frames go to:
   - a WAV file (--audio-dump), the same bytes on every run with the same input;
   - the sound device (SDL2, with a window), queued as they are made. The game is held to 59.94
     vblanks a second, so they arrive at the rate the device plays them, a vblank's worth at a time.
     The device starts once 100 ms are queued, and if the queue ever runs dry it waits for 100 ms
     again, so that it never plays the gaps between vblanks. With --fast the frames arrive faster:
     past half a second queued, the queue is dropped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/audio.h"
#include "port/platform.h"
#include "sd.h"
#include "spu2.h"

#ifdef KF4_HAVE_SDL
#include <SDL.h>
#endif

void fsiopsnd_reset(void);
uint64_t fsiopsnd_next_tick(void);
void fsiopsnd_tick(uint64_t now);

#define CHUNK_FRAMES 512

static int initialised;
static FILE *wav;
static uint32_t wav_frames;

#ifdef KF4_HAVE_SDL
#define DEVICE_START_BYTES (SPU2_RATE * 4u / 10u)
#define DEVICE_MAX_BYTES (SPU2_RATE * 4u / 2u)

static SDL_AudioDeviceID device;
static int device_playing;
#endif

static void wav_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* The 44-byte header of 48 kHz 16-bit stereo PCM with `frames` frames. */
static void wav_header(uint8_t header[44], uint32_t frames) {
    uint32_t bytes = frames * 4u;

    memcpy(header, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
    wav_put32(header + 4, 36u + bytes);
    wav_put32(header + 24, SPU2_RATE);
    wav_put32(header + 28, SPU2_RATE * 4u);
    wav_put32(header + 40, bytes);
}

static void open_outputs(void) {
    if (port_options.audio_dump != NULL) {
        uint8_t header[44];

        wav = fopen(port_options.audio_dump, "wb");
        if (wav == NULL) {
            fprintf(stderr, "[audio] cannot write %s\n", port_options.audio_dump);
            exit(2);
        }
        wav_header(header, 0);
        fwrite(header, 1, sizeof(header), wav);
    }
#ifdef KF4_HAVE_SDL
    if (!port_options.headless && !port_options.no_audio) {
        SDL_AudioSpec want;
        SDL_AudioSpec have;

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "[audio] no sound: %s\n", SDL_GetError());
            return;
        }
        memset(&want, 0, sizeof(want));
        want.freq = SPU2_RATE;
        want.format = AUDIO_S16LSB;
        want.channels = 2;
        want.samples = 1024;
        device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (device == 0) {
            fprintf(stderr, "[audio] no sound device: %s\n", SDL_GetError());
            return;
        }
    }
#endif
}

static void output(const int16_t *frames, int count) {
    if (wav != NULL) {
        fwrite(frames, 4, (size_t)count, wav);
        wav_frames += (uint32_t)count;
    }
#ifdef KF4_HAVE_SDL
    if (device != 0) {
        Uint32 queued = SDL_GetQueuedAudioSize(device);

        if (device_playing && queued == 0) {
            /* ran dry: wait to have 100 ms again */
            SDL_PauseAudioDevice(device, 1);
            device_playing = 0;
        } else if (queued > DEVICE_MAX_BYTES) {
            SDL_ClearQueuedAudio(device);
        }
        SDL_QueueAudio(device, frames, (Uint32)count * 4u);
        if (!device_playing && SDL_GetQueuedAudioSize(device) >= DEVICE_START_BYTES) {
            SDL_PauseAudioDevice(device, 0);
            device_playing = 1;
        }
    }
#endif
}

void port_audio_finish(void) {
    if (wav != NULL) {
        uint8_t header[44];

        wav_header(header, wav_frames);
        fseek(wav, 0, SEEK_SET);
        fwrite(header, 1, sizeof(header), wav);
        fclose(wav);
        wav = NULL;
    }
#ifdef KF4_HAVE_SDL
    if (device != 0) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
#endif
}

void port_audio_init(void) {
    spu2_reset();
    port_audio_load_libsd();
    sd_reset();
    fsiopsnd_reset();
    open_outputs();
    initialised = 1;
    atexit(port_audio_finish);
}

void port_audio_sync(uint64_t now) {
    uint64_t target = now / AUDIO_EE_CYCLES_PER_FRAME;
    int16_t chunk[CHUNK_FRAMES * 2];

    if (!initialised) {
        return;
    }
    while (spu2_time() < target) {
        uint64_t left = target - spu2_time();
        int count = left < CHUNK_FRAMES ? (int)left : CHUNK_FRAMES;

        spu2_render(chunk, count);
        output(chunk, count);
    }
}

uint64_t port_audio_next_event(void) {
    return initialised ? fsiopsnd_next_tick() : UINT64_MAX;
}

void port_audio_event(uint64_t now) {
    port_audio_sync(now);
    fsiopsnd_tick(now);
}
