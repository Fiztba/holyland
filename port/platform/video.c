/* The window, and --dump-frames' pictures (port/include/port/platform.h).

   The GS's scanout (port/platform/gs.c) is read back on the DMA controller's thread (or, the native
   GS's, kept on the GPU) and queued here with its time; the window shows each when it is due (at each
   vblank the game's thread, waiting a little for the vblank's own; with --frame-rate above 60 the
   pictures between too, "The pictures between") in an SDL window, scaled to fit it at the TV's 4:3 (or stretched, by the settings), with
   the settings overlay (F1) over it. That is
   SDL_Renderer's work, or with --presenter vulkan a swapchain's on the native GS's own device
   (port/platform/present_vk.cpp), which takes the native GS's picture as the image it is on the GPU
   (port_video_submit_image) and the others' pixels as they are here. A dumped frame is
   written as a PNG, uncompressed, so no library is needed. It is the GS's own picture, 640x448 (or
   the scanout's size) whatever the window shows: with --widescreen it is the squeezed picture, to be
   shown at 16:9 (with --native-widescreen, the native GS's wider one, 4/3 or more as many pixels
   across as --native-scale gives). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define yield_thread() SwitchToThread()
#else
#include <pthread.h>
#include <sched.h>
#define yield_thread() sched_yield()
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

/* ---- The pictures, between the threads -------------------------------------------------------- */
#ifdef _WIN32
static SRWLOCK frame_lock = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&frame_lock)
#define UNLOCK() ReleaseSRWLockExclusive(&frame_lock)
/* the window's present: the game's thread, or the pictures between's (port_video_wait) */
static SRWLOCK present_lock = SRWLOCK_INIT;
#define PRESENT_LOCK() AcquireSRWLockExclusive(&present_lock)
#define PRESENT_UNLOCK() ReleaseSRWLockExclusive(&present_lock)
#else
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&frame_lock)
#define UNLOCK() pthread_mutex_unlock(&frame_lock)
static pthread_mutex_t present_lock = PTHREAD_MUTEX_INITIALIZER;
#define PRESENT_LOCK() pthread_mutex_lock(&present_lock)
#define PRESENT_UNLOCK() pthread_mutex_unlock(&present_lock)
#endif

#define VBLANK_SECONDS (1001.0 / 60000.0)

/* The pictures handed over and not yet shown, oldest first, each with its tag (PORT_PICTURE_TAG: its
   vblank, and when in it). A vblank's own picture is due at that vblank (port_video_vblank's wall time);
   with --frame-rate above 60 the pictures between two vblanks are due at their time between, and
   port_video_wait shows them then. A present shows the newest picture due and leaves out those before
   it (a picture that came late is not shown after a newer one). The queue holds a few vblanks'
   pictures: more, and the oldest goes. Pixels (SDL_Renderer, or paraLLEl-GS's) are copied in; the
   native GS's pictures on the GPU are only named (gsn_scanout_take_tag: it keeps the last 16). */
#define PICTURES 24
typedef struct {
    uint64_t tag;
    int image;        /* the native GS's image, not pixels */
    uint32_t *pixels; /* (the queue's own buffer, kept for the next) */
    size_t room;      /* in pixels */
    uint32_t width, height;
} Picture;
static Picture queue[PICTURES];
static int queue_first, queue_count;
static uint64_t newest_in;    /* 1 + the newest vblank a picture was handed over for (0: none yet) */
static int last_fresh;        /* the window's last present at a vblank had a new picture */
static uint64_t presented_vblank; /* the last vblank port_video_present ran at */
static int presenter = PORT_PRESENTER_SDL;

/* --stats: how the pictures reached the window (port_video_print_stats) */
static uint64_t stat_submitted, stat_overwritten, stat_fresh, stat_stale, stat_gaps[4], stat_left_out, stat_late;
static double stat_last, stat_sum, stat_sum2, stat_max;
static uint64_t stat_intervals, stat_vblanks_since, stat_between;

/* (under the lock) the queue's next place, the oldest left out when it is full */
static FILE *sim_log;

static Picture *queue_add(uint64_t tag) {
    Picture *p;

    if (sim_log != NULL) {
        fprintf(sim_log, "S %.6f %llu %.4f\n", port_wall_seconds(), (unsigned long long)PORT_PICTURE_VBLANK(tag),
                PORT_PICTURE_PHASE(tag));
    }
    stat_submitted++;
    if (queue_count == PICTURES) {
        stat_overwritten++; /* the oldest is never shown */
        queue_first = (queue_first + 1) % PICTURES;
        queue_count--;
    }
    p = &queue[(queue_first + queue_count) % PICTURES];
    queue_count++;
    p->tag = tag;
    if (PORT_PICTURE_VBLANK(tag) + 1 > newest_in) {
        __atomic_store_n(&newest_in, PORT_PICTURE_VBLANK(tag) + 1, __ATOMIC_RELEASE);
    }
    return p;
}

void port_video_submit(const PortGsFrame *frame, uint64_t tag) {
    size_t n = (size_t)frame->width * frame->height;
    Picture *p;

    LOCK();
    p = queue_add(tag);
    if (p->room < n) {
        free(p->pixels);
        p->pixels = malloc(sizeof(uint32_t) * n);
        p->room = p->pixels != NULL ? n : 0;
    }
    if (p->pixels != NULL) {
        memcpy(p->pixels, frame->rgba, sizeof(uint32_t) * n);
        p->width = frame->width;
        p->height = frame->height;
    } else {
        p->width = p->height = 0;
    }
    p->image = 0;
    UNLOCK();
}

void port_video_submit_image(uint64_t tag) {
    LOCK();
    queue_add(tag)->image = 1;
    UNLOCK();
}

int port_video_takes_images(void) {
    return __atomic_load_n(&presenter, __ATOMIC_ACQUIRE) == PORT_PRESENTER_VULKAN;
}

int port_video_presenter(void) {
    return __atomic_load_n(&presenter, __ATOMIC_ACQUIRE);
}

/* ---- When each picture is due (the game's thread) ---------------------------------------------- */
#define TIMES 64
static uint64_t time_vblank[TIMES];
static double time_wall[TIMES];
static uint64_t times_newest;
static int times_set;

void port_video_vblank(uint64_t vblank, double wall) {
    LOCK();
    time_vblank[vblank % TIMES] = vblank;
    time_wall[vblank % TIMES] = wall;
    times_newest = vblank;
    times_set = 1;
    UNLOCK();
}

/* A picture's wall time: its vblank's, and its phase of a vblank after it */
static double picture_time(uint64_t tag) {
    uint64_t v = PORT_PICTURE_VBLANK(tag);
    double base;

    if (!times_set) {
        return 0;
    }
    if (time_vblank[v % TIMES] == v) {
        base = time_wall[v % TIMES];
    } else {
        base = time_wall[times_newest % TIMES] + ((double)v - (double)times_newest) * VBLANK_SECONDS;
    }
    return base + PORT_PICTURE_PHASE(tag) * VBLANK_SECONDS;
}

static int refresh_rate; /* the window's screen's (port_video_refresh_rate) */

/* KF4_PRESENT_SIM=HZ, a test of the pacing without a window (--headless): pictures are handed over and
   presented at their times as for the Vulkan presenter, with nothing drawn; the screen's refresh rate
   is HZ (0: none); --stats counts them and KF4_PRESENT_LOG=FILE writes each present's wall time and
   the picture's vblank and phase */
static int sim;
static FILE *sim_log;
#ifdef KF4_HAVE_SDL
static void between_start(void);
#endif

int port_video_simulated(void) {
    return sim;
}

int port_video_refresh_rate(void) {
    return __atomic_load_n(&refresh_rate, __ATOMIC_ACQUIRE);
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
static uint32_t shown_width, shown_height; /* its size */

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

#ifdef KF4_HAVE_SDL
/* The window's screen's refresh rate, for --frame-rate monitor */
static void read_refresh_rate(void) {
    SDL_DisplayMode mode;
    int display = SDL_GetWindowDisplayIndex(window);

    if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0) {
        __atomic_store_n(&refresh_rate, mode.refresh_rate, __ATOMIC_RELEASE);
    }
}
#endif

void port_video_apply_settings(void) {
#ifdef KF4_HAVE_SDL
    int width, height;

    if (window == NULL) {
        return;
    }
    read_refresh_rate();
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
    if (port_options.headless && port_gs_enabled() && getenv("KF4_PRESENT_SIM") != NULL) {
        sim = 1;
        refresh_rate = atoi(getenv("KF4_PRESENT_SIM"));
        __atomic_store_n(&presenter, PORT_PRESENTER_VULKAN, __ATOMIC_RELEASE);
        if (getenv("KF4_PRESENT_LOG") != NULL) {
            sim_log = fopen(getenv("KF4_PRESENT_LOG"), "w");
        }
        between_start();
        return;
    }
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
    /* the Vulkan presenter on the window, if it starts; else the window again, for SDL_Renderer. It
       needs the native GS's device: with paraLLEl-GS alone (--renderer gs, or the native GS did not
       start; port_options.renderer is what started) the window is SDL_Renderer's, quietly unless
       --presenter vulkan asked for it */
    if (port_options.presenter == PORT_PRESENTER_VULKAN && port_options.renderer == PORT_RENDERER_GS) {
        if (port_options.presenter_forced) {
            fprintf(stderr, "[video] --presenter vulkan needs the native GS: the window is SDL_Renderer's\n");
        }
    } else if (port_options.presenter == PORT_PRESENTER_VULKAN) {
        window = make_window(SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
        if (window != NULL && port_vk_open(window)) {
            shown = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
            __atomic_store_n(&presenter, PORT_PRESENTER_VULKAN, __ATOMIC_RELEASE);
            port_overlay_init(window, NULL);
            read_refresh_rate();
            between_start();
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
    read_refresh_rate();
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

#ifdef KF4_HAVE_SDL
/* Takes the picture to show: the newest whose time is no later than `until` (with --fast, the newest),
   those before it left out. 1 if there is one: its pixels into shown (or image: the native GS's, by
   its tag). */
static int take_picture(double until, int *image, uint64_t *tag) {
    int i, pick = -1;

    LOCK();
    for (i = 0; i < queue_count; i++) {
        const Picture *p = &queue[(queue_first + i) % PICTURES];

        if (port_options.fast || picture_time(p->tag) <= until) {
            pick = i;
        }
    }
    if (pick >= 0) {
        Picture *p = &queue[(queue_first + pick) % PICTURES];

        stat_left_out += (uint64_t)pick;
        *image = p->image;
        *tag = p->tag;
        if (!p->image && p->width != 0) {
            shown_width = p->width;
            shown_height = p->height;
            memcpy(shown, p->pixels, sizeof(uint32_t) * shown_width * shown_height);
        }
        queue_first = (queue_first + pick + 1) % PICTURES;
        queue_count -= pick + 1;
    }
    UNLOCK();
    return pick >= 0;
}

/* The window drawn: a new picture (fresh: its pixels in shown, or image: the native GS's by its tag),
   or the last again for the overlay; with overlay, the overlay and the mouse indicator over it (the
   game's thread only). Under PRESENT_LOCK. */
static void draw_window(int fresh, int image, uint64_t tag, int overlay) {
    uint32_t width = shown_width, height = shown_height;

    if (port_options.stats && fresh) {
        /* the wall time between new pictures */
        double now = port_wall_seconds();

        stat_fresh++;
        if (stat_last > 0) {
            double d = now - stat_last;

            stat_intervals++;
            stat_sum += d;
            stat_sum2 += d * d;
            stat_max = d > stat_max ? d : stat_max;
            stat_late += times_set && now - picture_time(tag) > 0.5 * VBLANK_SECONDS;
        }
        stat_last = now;
    }
    if (sim) {
        if (sim_log != NULL && fresh) {
            fprintf(sim_log, "%.6f %llu %.4f\n", port_wall_seconds(), (unsigned long long)PORT_PICTURE_VBLANK(tag),
                    PORT_PICTURE_PHASE(tag));
        }
        return;
    }
#ifdef KF4_HAVE_VULKAN_PRESENTER
    if (presenter == PORT_PRESENTER_VULKAN) {
        /* (the presenter draws again with nothing new only for the overlay, as below) */
        port_vk_present(fresh && !image ? shown : NULL, width, height, fresh && image, tag, overlay);
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
}
#endif

#ifdef KF4_HAVE_SDL
static int between_on(void);
static int thread_shows(void);
#endif

void port_video_present(void) {
#ifdef KF4_HAVE_SDL
    int fresh, image = 0;
    uint64_t tag = 0;

    if (window == NULL && !sim) {
        return;
    }
    if (between_on() && thread_shows()) {
        /* (the pictures' thread shows this vblank's too, a little after: port_video_wait) */
        LOCK();
        presented_vblank = times_newest;
        UNLOCK();
        return;
    }
    /* The picture of the vblank just begun comes from the renderer's thread about when this one wakes
       for it, sometimes a little after. Shown at the next vblank instead, the one before was shown
       twice. So while pictures are coming (the last present had one) and none of this vblank's has come,
       the window waits for it, up to half a vblank; not with --fast, which does not wait for the
       renderer (it is held at most two vblanks behind, gsn_vsync). */
    if (!port_options.fast && last_fresh && times_set) {
        double until = port_wall_seconds() + 0.5 * VBLANK_SECONDS;

        while (__atomic_load_n(&newest_in, __ATOMIC_ACQUIRE) <= times_newest && port_wall_seconds() < until) {
            port_sleep(0.00025);
        }
    }
    PRESENT_LOCK();
    fresh = take_picture(port_wall_seconds() + 0.001, &image, &tag);
    last_fresh = fresh;
    LOCK();
    presented_vblank = times_newest;
    UNLOCK();
    if (port_options.stats) {
        /* a new picture at this vblank, or the last one again */
        stat_vblanks_since++;
        if (fresh) {
            stat_gaps[stat_vblanks_since < 4 ? stat_vblanks_since : 3]++;
            stat_vblanks_since = 0;
        } else {
            stat_stale++;
        }
    }
    draw_window(fresh, image, tag, 1);
    PRESENT_UNLOCK();
#endif
}

#ifdef KF4_HAVE_SDL
/* --frame-rate above 60: the pictures between two vblanks, each shown at its time. With the Vulkan
   presenter a thread of their own shows them, so that the game's own work between two vblanks (a few
   milliseconds, often without a call into the kernel) does not hold them up; it leaves them to the
   game's thread while the overlay or the mouse indicator is drawn (their ImGui frame is the game
   thread's), and with SDL_Renderer (which is the game thread's) they are the game thread's always:
   shown while it waits for the next vblank (port_video_wait) and whenever it is in the kernel meanwhile
   (port_video_poll). (At 60 and below every picture is a vblank's own, shown by port_video_present.) */
static int between_thread_up;
static volatile int between_stop;
#ifdef _WIN32
static HANDLE between_handle;
#else
static pthread_t between_handle;
#endif

static int between_on(void) {
    return (window != NULL || sim) && !port_options.fast && times_set && port_frame_interp() && port_frame_rate_hz() > 60;
}

/* the thread shows them now */
static int thread_shows(void) {
    return between_thread_up && !port_overlay_visible() && !port_controls_indicator_shown();
}

/* The thread shows each picture a quarter of a vblank after its time (the vblank's own too): the
   renderer makes them one after another from the vblank on, and the first between is due a quarter of
   a vblank (at 240) after it; so each is there before it is due, and they are shown evenly. */
#define THREAD_DELAY (0.25 * VBLANK_SECONDS)

/* Shows the picture that is due at `now`, if there is one: from the game's thread, a picture between
   (not the next vblank's own, which is port_video_present's); from the thread, any, THREAD_DELAY after
   its time. 1 if it did. *first: when the oldest picture waiting is due (-1: none). */
static int show_between(double now, double *first, int from_thread) {
    double before, until, delay = from_thread ? THREAD_DELAY : 0;
    int image = 0, i, pick = -1;
    uint64_t tag = 0;

    LOCK();
    before = from_thread ? 1e30 : picture_time(PORT_PICTURE_TAG(presented_vblank + 1, 0)) - 0.0005;
    until = now - delay + 0.0003 < before ? now - delay + 0.0003 : before;
    *first = queue_count > 0 ? picture_time(queue[queue_first].tag) + delay : -1;
    for (i = 0; i < queue_count; i++) {
        const Picture *p = &queue[(queue_first + i) % PICTURES];

        if (picture_time(p->tag) <= until) {
            pick = i;
        }
    }
    UNLOCK();
    if (pick < 0) {
        return 0;
    }
    PRESENT_LOCK();
    if (!take_picture(until, &image, &tag)) {
        PRESENT_UNLOCK();
        return 0;
    }
    stat_between += PORT_PICTURE_PHASE(tag) > 0;
    draw_window(1, image, tag, !from_thread);
    PRESENT_UNLOCK();
    return 1;
}

/* Until `until` (the wall time), shows the pictures between as they are due */
static void show_until(double until, int from_thread) {
    for (;;) {
        double now = port_wall_seconds(), next = until, first;

        if (now >= until - 0.0002 || (from_thread && between_stop)) {
            break;
        }
        if (show_between(now, &first, from_thread)) {
            continue;
        }
        if (first > now && first < next) {
            next = first;
        }
        if (next > now + 0.002) {
            next = now + 0.002; /* (a picture may still come) */
        }
        /* asleep to half a millisecond before, then the rest in a spin: a sleep wakes up to half a
           millisecond late, an eighth of a picture's time at 240 */
        if (next - now > 0.0007) {
            port_sleep(next - now - 0.0005);
        } else {
            while (port_wall_seconds() < next) {
                yield_thread();
            }
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI between_main(LPVOID unused) {
#else
static void *between_main(void *unused) {
#endif
    (void)unused;
    while (!between_stop) {
        if (between_on() && thread_shows()) {
            show_until(port_wall_seconds() + 0.01, 1);
        } else {
            port_sleep(0.002);
        }
    }
    return 0;
}

static void between_end(void) {
    if (between_thread_up) {
        between_stop = 1;
#ifdef _WIN32
        WaitForSingleObject(between_handle, 1000);
#else
        pthread_join(between_handle, NULL);
#endif
        between_thread_up = 0;
    }
}

/* (port_video_init) the thread, for the Vulkan presenter */
static void between_start(void) {
#ifdef _WIN32
    between_handle = CreateThread(NULL, 0, between_main, NULL, 0, NULL);
    between_thread_up = between_handle != NULL;
#else
    between_thread_up = pthread_create(&between_handle, NULL, between_main, NULL) == 0;
#endif
    if (between_thread_up) {
        atexit(between_end);
    }
}
#endif

void port_video_poll(void) {
#ifdef KF4_HAVE_SDL
    static double next_look;
    double now, first;

    if (!between_on() || thread_shows()) {
        return;
    }
    now = port_wall_seconds();
    if (now < next_look) {
        return;
    }
    next_look = now + 0.00025;
    show_between(now, &first, 0);
#endif
}

void port_video_wait(double until) {
#ifdef KF4_HAVE_SDL
    if (!between_on() || thread_shows()) {
        return;
    }
    show_until(until, 0);
#else
    (void)until;
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

void port_video_print_stats(void) {
#ifdef KF4_HAVE_SDL
    double mean = stat_intervals ? stat_sum / (double)stat_intervals : 0;
    double sd = stat_intervals ? sqrt(stat_sum2 / (double)stat_intervals - mean * mean) : 0;

    if (window == NULL && !sim) {
        return;
    }
    fprintf(stderr,
            "[stats] window: %llu pictures handed over, %llu of them dropped (the queue full), %llu left out (a newer "
            "one due); %llu vblanks showed a new one (1, 2, 3+ vblanks after the last: %llu, %llu, %llu), %llu the "
            "last again; %llu pictures between vblanks shown; between new pictures %.2f ms (sd %.2f, most %.2f); %llu "
            "shown more than half a vblank after their time\n",
            (unsigned long long)stat_submitted, (unsigned long long)stat_overwritten, (unsigned long long)stat_left_out,
            (unsigned long long)(stat_gaps[1] + stat_gaps[2] + stat_gaps[3]), (unsigned long long)stat_gaps[1],
            (unsigned long long)stat_gaps[2], (unsigned long long)stat_gaps[3], (unsigned long long)stat_stale,
            (unsigned long long)stat_between, mean * 1e3, sd * 1e3, stat_max * 1e3, (unsigned long long)stat_late);
#endif
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
