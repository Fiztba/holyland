/* The debugging tools' common parts (port/include/port/debug.h): the captures folder, the message the
   overlay shows, F10's state dump and F12's sound. The GS capture is port/debug/gs_capture.c. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "port/audio.h"
#include "port/debug.h"
#include "port/platform.h"
#include "port/settings.h"

void port_gs_capture_init(const char *list);

/* ---- The message ------------------------------------------------------------------------------ */
static char message[1400] = "nothing written yet";

void port_debug_set_message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void port_debug_set_message(const char *fmt, ...) {
    char text[sizeof(message)];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    memcpy(message, text, sizeof(message));
    fprintf(stderr, "[debug] %s\n", text);
}

const char *port_debug_message(void) {
    return message;
}

/* ---- The captures folder: <data folder>/captures ------------------------------------------------ */
const char *port_debug_folder(void) {
    static char path[1100];

    if (path[0] == 0) {
        snprintf(path, sizeof(path), "%s/captures", port_data_dir());
    }
    return path;
}

static void make_folder(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* <captures>/<stem>_<vblank>_<local time>.<extension>, the folder made if it is missing */
const char *port_debug_capture_path(const char *stem, uint64_t vblank, const char *extension, char *out,
                                    size_t size) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    char when[32] = "0";

    make_folder(port_data_dir());
    make_folder(port_debug_folder());
    if (local != NULL) {
        strftime(when, sizeof(when), "%Y%m%d-%H%M%S", local);
    }
    snprintf(out, size, "%s/%s_%06llu_%s.%s", port_debug_folder(), stem, (unsigned long long)vblank, when,
             extension);
    return out;
}

/* KF4_DEBUG_AT=state@VBLANK,audio@VBLANK,gs@VBLANK,...: press F10, F12 or F9 at those vblanks, for
   checking the tools without anyone at the keyboard (in vblank order). Run on the game's thread at the
   vblank, from the GS capture's per-vblank note (port/platform/gs.c), so only in a run that draws. */
#define DEBUG_AT 16
static struct {
    uint64_t vblank;
    char what;
} debug_at[DEBUG_AT];
static int debug_at_count, debug_at_next;

int port_debug_at_pending(void) {
    return debug_at_next < debug_at_count;
}

void port_debug_at_vblank(uint64_t vblank) {
    while (debug_at_next < debug_at_count && debug_at[debug_at_next].vblank <= vblank) {
        char what = debug_at[debug_at_next++].what;

        if (what == 's') {
            port_debug_write_state_dump();
        } else if (what == 'a') {
            port_debug_write_audio_tail();
        } else {
            port_debug_request_gs_capture();
        }
    }
}

void port_debug_init(const char *gs_capture_at) {
    const char *p = getenv("KF4_DEBUG_AT");

    port_gs_capture_init(gs_capture_at);
    while (p != NULL && *p && debug_at_count < DEBUG_AT) {
        const char *at = strchr(p, '@');

        if (at == NULL || (*p != 's' && *p != 'a' && *p != 'g')) {
            fprintf(stderr, "KF4_DEBUG_AT wants state@VBLANK, audio@VBLANK or gs@VBLANK joined by commas\n");
            exit(2);
        }
        debug_at[debug_at_count].what = *p;
        debug_at[debug_at_count].vblank = strtoull(at + 1, (char **)&p, 0);
        debug_at_count++;
        if (*p == ',') {
            p++;
        }
    }
    if (debug_at_count) {
        port_gs_capture_armed = 1;
    }
}

/* ---- F10: the game's state as JSON ------------------------------------------------------------- */
/* the last events the port logged (port_event: files read, movies, the memory card), --log or not */
#define EVENTS 32
static char events[EVENTS][256];
static uint64_t event_count;

void port_debug_note_event(uint64_t vblank, const char *fmt, va_list args) {
    char *line = events[event_count % EVENTS];
    int n = snprintf(line, sizeof(events[0]), "%llu ", (unsigned long long)vblank);

    vsnprintf(line + n, sizeof(events[0]) - (size_t)n, fmt, args);
    event_count++;
}

static void json_string(FILE *file, const char *text) {
    fputc('"', file);
    for (; *text; text++) {
        if (*text == '"' || *text == '\\') {
            fputc('\\', file);
            fputc(*text, file);
        } else if ((unsigned char)*text < 0x20) {
            fprintf(file, "\\u%04x", (unsigned char)*text);
        } else {
            fputc(*text, file);
        }
    }
    fputc('"', file);
}

void port_debug_write_state_dump(void) {
    char path[1200];
    FILE *file;
    uint64_t first = event_count > EVENTS ? event_count - EVENTS : 0, n;

    port_debug_capture_path("state", port_vblank_count(), "json", path, sizeof(path));
    file = fopen(path, "w");
    if (file == NULL) {
        port_debug_set_message("cannot write %s", path);
        return;
    }
    fprintf(file, "{\n  \"vblank\": %llu,\n  \"events\": [", (unsigned long long)port_vblank_count());
    for (n = first; n < event_count; n++) {
        fprintf(file, "%s\n    ", n == first ? "" : ",");
        json_string(file, events[n % EVENTS]);
    }
    fprintf(file, "\n  ],\n");
    kf4_game_dump(file);
    fprintf(file, "}\n");
    fclose(file);
    port_debug_set_message("game state written: %s", path);
}

/* ---- F12: the last 20 seconds of the sound ------------------------------------------------------
   Every frame the SPU2 makes goes into a ring (port/audio/output.c calls in, under the sound's lock,
   on whichever thread renders), as made: before the player's volume. */
#define TAIL_FRAMES (48000u * 20u)

static int16_t *tail;      /* TAIL_FRAMES stereo frames */
static uint32_t tail_at;   /* the next frame written */
static uint64_t tail_made; /* frames so far */

void port_debug_audio_frames(const int16_t *frames, int count) {
    if (tail == NULL) {
        tail = calloc(TAIL_FRAMES, 4);
        if (tail == NULL) {
            return;
        }
    }
    while (count > 0) {
        uint32_t n = TAIL_FRAMES - tail_at;

        if (n > (uint32_t)count) {
            n = (uint32_t)count;
        }
        memcpy(tail + (size_t)tail_at * 2, frames, (size_t)n * 4);
        frames += n * 2;
        count -= (int)n;
        tail_at = (tail_at + n) % TAIL_FRAMES;
        tail_made += n;
    }
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void port_debug_write_audio_tail(void) {
    char path[1200];
    uint8_t header[44];
    int16_t *copy;
    uint32_t frames, first;
    FILE *file;

    port_audio_lock();
    frames = tail_made < TAIL_FRAMES ? (uint32_t)tail_made : TAIL_FRAMES;
    first = tail_made < TAIL_FRAMES ? 0 : tail_at;
    copy = frames ? malloc((size_t)frames * 4) : NULL;
    if (copy != NULL) {
        uint32_t n = TAIL_FRAMES - first < frames ? TAIL_FRAMES - first : frames;

        memcpy(copy, tail + (size_t)first * 2, (size_t)n * 4);
        memcpy(copy + (size_t)n * 2, tail, (size_t)(frames - n) * 4);
    }
    port_audio_unlock();
    if (copy == NULL) {
        port_debug_set_message("no sound made yet");
        return;
    }
    port_debug_capture_path("audio", port_vblank_count(), "wav", path, sizeof(path));
    memcpy(header, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data\0\0\0\0", 44);
    put32(header + 4, 36u + frames * 4u);
    put32(header + 24, 48000u);
    put32(header + 28, 48000u * 4u);
    put32(header + 40, frames * 4u);
    file = fopen(path, "wb");
    if (file == NULL || fwrite(header, 1, 44, file) != 44 || fwrite(copy, 4, frames, file) != frames) {
        port_debug_set_message("cannot write %s", path);
    } else {
        port_debug_set_message("the last %.1f s of sound written: %s", frames / 48000.0, path);
    }
    if (file != NULL) {
        fclose(file);
    }
    free(copy);
}
