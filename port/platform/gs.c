/* The GS: the GIF's three paths and the display, on the DMA controller's thread.

   Packets come from VU1's XGKICK (PATH1, port/platform/vif1.c), VIF1's DIRECT (PATH2) and DMA
   channel 2 (PATH3, port/platform/dmac.c), and go to the backend in port/gs/gs_parallel.cpp
   (paraLLEl-GS). At each vblank the kernel snapshots the display registers (PMODE..BGCOLOR in the
   register page) and queues them here; the controller's thread finishes the channels started before
   it, then has the backend scan the frame out, and hands the picture to the window
   (port/platform/video.c) and to --dump-frames.

   Without the backend (no Vulkan, or a --headless run that dumps nothing) the packets are read all
   the same, so VIF1 and VU1 run, and then dropped. What the game does never depends on it: the
   picture is all the GS gives back, apart from the textures sceGsExecStoreImage reads, which the
   game only sends to the GS again. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"

#ifdef KF4_HAVE_PARALLEL_GS
int port_gs_backend_open(void);
void port_gs_backend_gif(int path, const void *data, size_t bytes);
void port_gs_backend_read_fifo(void *dst, uint32_t qwords);
int port_gs_backend_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);
#endif

#define VSYNC_QUEUE 16

typedef struct {
    uint64_t privileged[0xF0 / 8];
    uint64_t vblank;
    int field;
} VsyncRequest;

static int backend; /* the renderer is up */
static VsyncRequest queue[VSYNC_QUEUE];
static volatile uint64_t queue_head; /* written by the game thread */
static volatile uint64_t queue_tail; /* written by the controller's thread */
static PortGsFrame frame;

/* --dump-frames: the vblanks whose picture is written, sorted */
static uint64_t *dump_at;
static int dump_count;
static int dump_next;

static void parse_dumps(void) {
    const char *p = port_options.dump_frames;

    if (p == NULL) {
        return;
    }
    while (*p) {
        char *end;
        unsigned long long v = strtoull(p, &end, 0);

        if (end == p) {
            fprintf(stderr, "--dump-frames wants vblank numbers joined by commas, not %s\n", port_options.dump_frames);
            exit(2);
        }
        dump_at = realloc(dump_at, sizeof(*dump_at) * (size_t)(dump_count + 1));
        dump_at[dump_count++] = v;
        p = *end == ',' ? end + 1 : end;
    }
}

static void open_backend(void *unused) {
    (void)unused;
#ifdef KF4_HAVE_PARALLEL_GS
    backend = port_gs_backend_open();
#endif
}

void port_gs_init(void) {
    parse_dumps();
    if (port_options.no_gs || (port_options.headless && dump_count == 0)) {
        return;
    }
    port_dmac_call(open_backend, NULL);
    if (!backend) {
        fprintf(stderr, "[gs] no renderer: the game runs, but nothing is drawn\n");
    }
}

int port_gs_enabled(void) {
    return backend;
}

void port_gs_gif(int path, const void *data, size_t bytes) {
#ifdef KF4_HAVE_PARALLEL_GS
    if (backend) {
        port_gs_backend_gif(path, data, bytes);
    }
#else
    (void)path;
    (void)data;
    (void)bytes;
#endif
}

void port_gs_read_fifo(void *dst, uint32_t qwords) {
#ifdef KF4_HAVE_PARALLEL_GS
    if (backend) {
        port_gs_backend_read_fifo(dst, qwords);
        return;
    }
#endif
    memset(dst, 0, (size_t)qwords * 16);
}

/* ---- Vblanks ---------------------------------------------------------------------------------- */
void port_gs_post_vsync(void) {
    VsyncRequest *request;
    uint64_t head;

    if (!backend) {
        return;
    }
    /* The scanout goes between the packets started before the vblank and those after: the game
       (and --fast all the more) runs ahead of the controller, and would have cleared the buffer
       shown by the time it was read. So the ones before are finished first, and the controller
       scans out before it reads a channel it finds started while this is queued (dmac.c). */
    port_dmac_wait(1);
    port_dmac_wait(2);
    head = __atomic_load_n(&queue_head, __ATOMIC_RELAXED);
    while (head - __atomic_load_n(&queue_tail, __ATOMIC_ACQUIRE) >= VSYNC_QUEUE) {
        port_dmac_wake(); /* the GS is a whole queue behind: let it catch up */
    }
    request = &queue[head % VSYNC_QUEUE];
    memcpy(request->privileged, ee_hw_ptr(0x12000000), sizeof(request->privileged));
    request->vblank = port_vblank_count();
    request->field = (int)((*(volatile uint64_t *)ee_hw_ptr(0x12001000) >> 13) & 1);
    __atomic_store_n(&queue_head, head + 1, __ATOMIC_RELEASE);
    port_dmac_wake();
}

void port_gs_finish(void) {
    while (backend && port_gs_vsync_pending()) {
        port_dmac_wake();
    }
}

int port_gs_vsync_pending(void) {
    return __atomic_load_n(&queue_head, __ATOMIC_ACQUIRE) != __atomic_load_n(&queue_tail, __ATOMIC_RELAXED);
}

void port_gs_run_vsync(void) {
    uint64_t tail = __atomic_load_n(&queue_tail, __ATOMIC_RELAXED);
#ifdef KF4_HAVE_PARALLEL_GS
    VsyncRequest *request = &queue[tail % VSYNC_QUEUE];
    int dump = 0;
    int shown = !port_options.headless;

    while (dump_next < dump_count && dump_at[dump_next] < request->vblank) {
        dump_next++;
    }
    if (dump_next < dump_count && dump_at[dump_next] == request->vblank) {
        dump = 1;
    }
    if (frame.rgba == NULL) {
        frame.rgba = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
    }
    if (port_gs_backend_vsync(request->privileged, request->field, shown || dump, &frame)) {
        if (dump) {
            port_video_dump(&frame, request->vblank);
        }
        if (shown) {
            port_video_submit(&frame);
        }
    } else if (dump) {
        fprintf(stderr, "[gs] vblank %llu: nothing is shown, no picture to dump\n", (unsigned long long)request->vblank);
    }
#endif
    __atomic_store_n(&queue_tail, tail + 1, __ATOMIC_RELEASE);
}
