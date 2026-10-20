/* The window, and --dump-frames' pictures (port/include/port/platform.h).

   The GS's scanout (port/platform/gs.c) is read back on the DMA controller's thread and left here;
   at each vblank the game's thread shows the newest one in an SDL window, scaled to fit it at the
   TV's 4:3 (or stretched, by the settings), with the settings overlay (F1) over it. That is
   SDL_Renderer's work, or with --presenter vulkan a swapchain's on the native GS's own device
   (port/platform/present_vk.cpp), which takes the native GS's picture as the image it is on the GPU
   (port_video_submit_image) and the others' pixels as they are here. A dumped frame is
   written as a PNG, uncompressed, so no library is needed. It is the GS's own picture, 640x448 (or
   the scanout's size) whatever the window shows: with --widescreen it is the squeezed picture, to be
   shown at 16:9 (with --native-widescreen, the native GS's wider one, 4/3 or more as many pixels
   across as --native-scale gives). */
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

#include "port/controls.h"
#include "port/host.h"
#include "port/overlay.h"
#include "port/platform.h"
#include "port/present_vk.h"
#include "port/settings.h"

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
static int latest_image; /* the newest picture is the native GS's image on the GPU, not latest */
static int presenter = PORT_PRESENTER_SDL;

void port_video_submit(const PortGsFrame *frame) {
    LOCK();
    if (latest == NULL) {
        latest = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
    }
    memcpy(latest, frame->rgba, sizeof(uint32_t) * frame->width * frame->height);
    latest_width = frame->width;
    latest_height = frame->height;
    latest_new = 1;
    latest_image = 0;
    UNLOCK();
}

void port_video_submit_image(void) {
    LOCK();
    latest_new = 1;
    latest_image = 1;
    UNLOCK();
}

int port_video_takes_images(void) {
    return __atomic_load_n(&presenter, __ATOMIC_ACQUIRE) == PORT_PRESENTER_VULKAN;
}

int port_video_presenter(void) {
    return __atomic_load_n(&presenter, __ATOMIC_ACQUIRE);
}

#ifdef KF4_HAVE_SDL
/* The picture's place in a window of out_w x out_h pixels: the TV's 4:3, whatever the scanout's size;
   with --widescreen a 16:9 TV's (the game drew its picture squeezed for it, port/platform/gs.c); or
   the whole window, stretched */
void port_video_fit(int out_w, int out_h, int *x, int *y, int *width, int *height) {
    double aspect = port_options.widescreen ? 16.0 / 9.0 : 4.0 / 3.0;

    if (port_settings.aspect == ASPECT_STRETCH) {
        *width = out_w;
        *height = out_h;
    } else if ((double)out_w / out_h > aspect) {
        *height = out_h;
        *width = (int)(out_h * aspect + 0.5);
    } else {
        *width = out_w;
        *height = (int)(out_w / aspect + 0.5);
    }
    *x = (out_w - *width) / 2;
    *y = (out_h - *height) / 2;
}
#endif

/* ---- The window ------------------------------------------------------------------------------- */
#ifdef KF4_HAVE_SDL
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static uint32_t texture_width, texture_height;
static int texture_smooth;
static uint32_t *shown;

/* the window's size in windowed mode: the height the settings say, and the width they say (a
   monitor's size, which the picture is fitted into) or as wide as the picture's shape (4:3, or 16:9
   with --widescreen) at that height */
static void window_size(int *width, int *height) {
    int h = port_settings.window_height < 448 ? 448 : port_settings.window_height;

    *width = port_settings.window_width > 0 ? port_settings.window_width
             : port_options.widescreen     ? (h * 16 + 8) / 9
                                            : (h * 4 + 2) / 3;
    *height = h;
}
#endif

void port_video_apply_settings(void) {
#ifdef KF4_HAVE_SDL
    int width, height;

    if (window == NULL) {
        return;
    }
    SDL_SetWindowFullscreen(window, port_settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!port_settings.fullscreen) {
        window_size(&width, &height);
        SDL_SetWindowSize(window, width, height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    if (renderer != NULL) {
        SDL_RenderSetVSync(renderer, port_settings.vsync);
    }
#ifdef KF4_HAVE_VULKAN_PRESENTER
    if (presenter == PORT_PRESENTER_VULKAN) {
        port_vk_set_vsync(port_settings.vsync);
    }
#endif
#endif
}

#ifdef KF4_HAVE_SDL
static SDL_Window *make_window(Uint32 flags) {
    int width, height;

    window_size(&width, &height);
    if (port_settings.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    /* pictures of the overlay for tests (KF4_OVERLAY_SHOT) need no window on the screen */
    if (getenv("KF4_OVERLAY_SHOT") != NULL) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    return SDL_CreateWindow("King's Field IV", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
}
#endif

void port_video_init(void) {
#ifdef KF4_HAVE_SDL
    if (port_options.headless || !port_gs_enabled()) {
        return;
    }
    /* the window's sizes are the screen's own pixels: without this, Windows at 150% scaling shows a
       2560 x 1440 monitor as 1707 x 960 and stretches the window's picture up to it */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[video] no window: %s\n", SDL_GetError());
        return;
    }
#ifdef KF4_HAVE_VULKAN_PRESENTER
    /* the Vulkan presenter on the window, if it starts; else the window again, for SDL_Renderer */
    if (port_options.presenter == PORT_PRESENTER_VULKAN) {
        window = make_window(SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
        if (window != NULL && port_vk_open(window)) {
            shown = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
            __atomic_store_n(&presenter, PORT_PRESENTER_VULKAN, __ATOMIC_RELEASE);
            port_overlay_init(window, NULL);
            return;
        }
        if (window == NULL) {
            fprintf(stderr, "[video] no Vulkan window: %s\n", SDL_GetError());
        }
        fprintf(stderr, "[video] the Vulkan presenter did not start: the window is SDL_Renderer's\n");
        if (window != NULL) {
            SDL_DestroyWindow(window);
        }
    }
#else
    if (port_options.presenter == PORT_PRESENTER_VULKAN) {
        fprintf(stderr, "[video] built without the Vulkan presenter: the window is SDL_Renderer's\n");
    }
#endif
    window = make_window(SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "[video] no window: %s\n", SDL_GetError());
        return;
    }
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | (port_settings.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
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
    port_overlay_init(window, renderer);
#else
    if (!port_options.headless && port_gs_enabled()) {
        fprintf(stderr, "[video] built without SDL2: no window\n");
    }
#endif
}

#ifdef KF4_HAVE_SDL
/* SDL_Renderer's square for the mouse indicator (port_controls_draw) */
static void fill_rect(void *context, int x, int y, int width, int height, unsigned int rgba) {
    SDL_Rect rect;

    (void)context;
    rect.x = x;
    rect.y = y;
    rect.w = width;
    rect.h = height;
    SDL_SetRenderDrawColor(renderer, (Uint8)rgba, (Uint8)(rgba >> 8), (Uint8)(rgba >> 16), (Uint8)(rgba >> 24));
    SDL_RenderFillRect(renderer, &rect);
}
#endif

void port_video_present(void) {
#ifdef KF4_HAVE_SDL
    uint32_t width, height;
    int fresh, image;

    if (window == NULL) {
        return;
    }
    LOCK();
    fresh = latest_new;
    image = latest_image;
    width = latest_width;
    height = latest_height;
    if (fresh && !image) {
        memcpy(shown, latest, sizeof(uint32_t) * width * height);
    }
    latest_new = 0;
    UNLOCK();
#ifdef KF4_HAVE_VULKAN_PRESENTER
    if (presenter == PORT_PRESENTER_VULKAN) {
        /* (the presenter draws again with nothing new only for the overlay, as below) */
        port_vk_present(fresh && !image ? shown : NULL, width, height, fresh && image);
        return;
    }
#endif
    /* with nothing new, the window is drawn again only for the overlay */
    if (!fresh && (texture == NULL || !port_overlay_visible())) {
        return;
    }
    if (fresh && (texture == NULL || texture_width != width || texture_height != height)) {
        if (texture != NULL) {
            SDL_DestroyTexture(texture);
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, (int)width,
                                    (int)height);
        texture_width = width;
        texture_height = height;
        texture_smooth = -1;
        if (texture == NULL) {
            return;
        }
    }
    if (fresh) {
        SDL_UpdateTexture(texture, NULL, shown, (int)(width * 4));
    }
    if (texture_smooth != port_settings.smooth) {
        texture_smooth = port_settings.smooth;
        SDL_SetTextureScaleMode(texture, texture_smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    {
        int out_w, out_h;
        SDL_Rect dst;
        SDL_BlendMode blend;

        SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
        port_video_fit(out_w, out_h, &dst.x, &dst.y, &dst.w, &dst.h);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        port_controls_draw(fill_rect, NULL, dst.x, dst.y, dst.w, dst.h);
        SDL_SetRenderDrawBlendMode(renderer, blend);
        port_overlay_draw(dst.x, dst.y, dst.w, dst.h);
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
int port_png_write(const char *path, const uint32_t *rgba, uint32_t width, uint32_t height) {
    size_t row = (size_t)width * 3 + 1;
    size_t raw_size = row * height;
    size_t blocks = (raw_size + 65534) / 65535;
    size_t z_size = 2 + raw_size + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_size);
    uint8_t *z = malloc(z_size);
    uint8_t header[13];
    uint32_t a = 1, b = 0;
    size_t i, at, done;
    uint32_t x, y;
    FILE *f;
    int ok = 0;

    for (y = 0; y < height; y++) {
        uint8_t *out = raw + row * y;

        *out++ = 0;
        for (x = 0; x < width; x++) {
            uint32_t p = rgba[(size_t)y * width + x];

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

    put32(header, width);
    put32(header + 4, height);
    header[8] = 8;  /* bits */
    header[9] = 2;  /* RGB */
    header[10] = 0; /* deflate */
    header[11] = 0; /* filters */
    header[12] = 0; /* not interlaced */

    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "[video] cannot write %s\n", path);
    } else {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        write_chunk(f, "IHDR", header, sizeof(header));
        write_chunk(f, "IDAT", z, at);
        write_chunk(f, "IEND", NULL, 0);
        fclose(f);
        ok = 1;
    }
    free(raw);
    free(z);
    return ok;
}

void port_video_dump(const PortGsFrame *frame, uint64_t vblank) {
    char name[64];

    snprintf(name, sizeof(name), "frame_%06llu.png", (unsigned long long)vblank);
    port_video_dump_named(frame, name);
}

void port_video_dump_named(const PortGsFrame *frame, const char *name) {
    char path[4096];

    snprintf(path, sizeof(path), "%s/%s", port_options.dump_dir ? port_options.dump_dir : ".", name);
    if (port_png_write(path, frame->rgba, frame->width, frame->height)) {
        fprintf(stderr, "[video] %ux%u, %s\n", frame->width, frame->height, path);
    }
}
