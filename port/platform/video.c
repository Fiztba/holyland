/* The window, and --dump-frames' pictures (port/include/port/platform.h).

   The GS's scanout (port/platform/gs.c) is read back on the DMA controller's thread and left here;
   at each vblank the game's thread shows the newest one in an SDL window, scaled to fit it at the
   picture's own aspect. A dumped frame is written as a PNG, uncompressed, so no library is needed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef KF4_HAVE_SDL
#include <SDL.h>
#endif

#include "port/host.h"
#include "port/platform.h"

/* ---- The newest picture, between the two threads --------------------------------------------- */
#ifdef _WIN32
static SRWLOCK frame_lock = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&frame_lock)
#define UNLOCK() ReleaseSRWLockExclusive(&frame_lock)
#else
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&frame_lock)
#define UNLOCK() pthread_mutex_unlock(&frame_lock)
#endif

static uint32_t *latest;
static uint32_t latest_width, latest_height;
static int latest_new;

void port_video_submit(const PortGsFrame *frame) {
    LOCK();
    if (latest == NULL) {
        latest = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
    }
    memcpy(latest, frame->rgba, sizeof(uint32_t) * frame->width * frame->height);
    latest_width = frame->width;
    latest_height = frame->height;
    latest_new = 1;
    UNLOCK();
}

/* ---- The window ------------------------------------------------------------------------------- */
#ifdef KF4_HAVE_SDL
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t texture_width, texture_height;
static uint32_t *shown;
#endif

void port_video_init(void) {
#ifdef KF4_HAVE_SDL
    if (port_options.headless || !port_gs_enabled()) {
        return;
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[video] no window: %s\n", SDL_GetError());
        return;
    }
    window = SDL_CreateWindow("King's Field IV", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 896,
                              SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "[video] no window: %s\n", SDL_GetError());
        return;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (renderer == NULL) {
        fprintf(stderr, "[video] no renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = NULL;
        return;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    shown = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
#else
    if (!port_options.headless && port_gs_enabled()) {
        fprintf(stderr, "[video] built without SDL2: no window\n");
    }
#endif
}

void port_video_present(void) {
#ifdef KF4_HAVE_SDL
    uint32_t width, height;
    int fresh;

    if (window == NULL) {
        return;
    }
    LOCK();
    fresh = latest_new;
    width = latest_width;
    height = latest_height;
    if (fresh) {
        memcpy(shown, latest, sizeof(uint32_t) * width * height);
        latest_new = 0;
    }
    UNLOCK();
    if (!fresh) {
        return;
    }
    if (texture == NULL || texture_width != width || texture_height != height) {
        if (texture != NULL) {
            SDL_DestroyTexture(texture);
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, (int)width,
                                    (int)height);
        texture_width = width;
        texture_height = height;
        if (texture == NULL) {
            return;
        }
    }
    SDL_UpdateTexture(texture, NULL, shown, (int)(width * 4));
    {
        int out_w, out_h;
        SDL_Rect dst;
        /* the TV's 4:3, whatever the scanout's size */
        double aspect = 4.0 / 3.0;

        SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
        if ((double)out_w / out_h > aspect) {
            dst.h = out_h;
            dst.w = (int)(out_h * aspect + 0.5);
        } else {
            dst.w = out_w;
            dst.h = (int)(out_w / aspect + 0.5);
        }
        dst.x = (out_w - dst.w) / 2;
        dst.y = (out_h - dst.h) / 2;
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_RenderPresent(renderer);
    }
#endif
}

/* ---- PNG -------------------------------------------------------------------------------------- */
static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i;

    if (crc_table[1] == 0) {
        uint32_t k;

        for (k = 0; k < 256; k++) {
            uint32_t c = k;
            int b;

            for (b = 0; b < 8; b++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            crc_table[k] = c;
        }
    }
    for (i = 0; i < n; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void write_chunk(FILE *f, const char *type, const uint8_t *data, size_t size) {
    uint8_t head[8];
    uint8_t tail[4];
    uint32_t crc;

    put32(head, (uint32_t)size);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    fwrite(data, 1, size, f);
    crc = crc32_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc32_update(crc, data, size) ^ 0xFFFFFFFFu;
    put32(tail, crc);
    fwrite(tail, 1, 4, f);
}

/* RGB rows, each after its filter byte (0), in a zlib stream of stored blocks */
void port_video_dump(const PortGsFrame *frame, uint64_t vblank) {
    char path[4096];
    size_t row = (size_t)frame->width * 3 + 1;
    size_t raw_size = row * frame->height;
    size_t blocks = (raw_size + 65534) / 65535;
    size_t z_size = 2 + raw_size + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_size);
    uint8_t *z = malloc(z_size);
    uint8_t header[13];
    uint32_t a = 1, b = 0;
    size_t i, at, done;
    uint32_t x, y;
    FILE *f;

    for (y = 0; y < frame->height; y++) {
        uint8_t *out = raw + row * y;

        *out++ = 0;
        for (x = 0; x < frame->width; x++) {
            uint32_t p = frame->rgba[(size_t)y * frame->width + x];

            *out++ = (uint8_t)p;
            *out++ = (uint8_t)(p >> 8);
            *out++ = (uint8_t)(p >> 16);
        }
    }
    z[0] = 0x78;
    z[1] = 0x01;
    at = 2;
    for (done = 0; done < raw_size;) {
        size_t n = raw_size - done > 65535 ? 65535 : raw_size - done;

        z[at++] = done + n == raw_size;
        z[at++] = (uint8_t)n;
        z[at++] = (uint8_t)(n >> 8);
        z[at++] = (uint8_t)~n;
        z[at++] = (uint8_t)(~n >> 8);
        memcpy(z + at, raw + done, n);
        at += n;
        done += n;
    }
    for (i = 0; i < raw_size; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32(z + at, (b << 16) | a);
    at += 4;

    put32(header, frame->width);
    put32(header + 4, frame->height);
    header[8] = 8;  /* bits */
    header[9] = 2;  /* RGB */
    header[10] = 0; /* deflate */
    header[11] = 0; /* filters */
    header[12] = 0; /* not interlaced */

    snprintf(path, sizeof(path), "%s/frame_%06llu.png", port_options.dump_dir ? port_options.dump_dir : ".",
             (unsigned long long)vblank);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "[video] cannot write %s\n", path);
    } else {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        write_chunk(f, "IHDR", header, sizeof(header));
        write_chunk(f, "IDAT", z, at);
        write_chunk(f, "IEND", NULL, 0);
        fclose(f);
        fprintf(stderr, "[video] vblank %llu: %ux%u, %s\n", (unsigned long long)vblank, frame->width,
                frame->height, path);
    }
    free(raw);
    free(z);
}
