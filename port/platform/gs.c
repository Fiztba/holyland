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

/* ---- The GIF ----------------------------------------------------------------------------------
   Each path's place in its GIF packet, read from the tags as the packets go by, with or without the
   backend: where the packets end (the GIF switches paths only there, port/platform/dmac.c), and the
   FINISH register, which sets CSR's FINISH once the GS has had it.

   The game clears FINISH as the hardware wants, by writing 1 to it (packet_buffer.c's GS_CSR |= 2),
   but nothing on the PC sees a store into the register page, so that sets the bit instead. It makes
   no difference to the game: it reads FINISH only after waiting for the channel whose chain ends with
   the FINISH register, and the GS has had it by then. */
#define GS_REG_FINISH 0x61
#define GS_CSR_FINISH 2

typedef struct {
    uint32_t left;   /* qwords of the current tag's data still to come */
    uint32_t flg;    /* its FLG: 0 PACKED, 1 REGLIST, 2 and 3 IMAGE */
    uint32_t nreg;   /* its NREG (1..16) */
    uint64_t regs;   /* its REGS */
    uint32_t reg;    /* PACKED: the register of the next qword */
    int ad;          /* PACKED: one of the registers is A+D */
    int eop;         /* the current tag ends its packet */
    int in_packet;   /* between a packet's first tag and its end */
} GifPath;

static GifPath gif_paths[4];

static void read_gif(int path, const uint64_t *qwords, size_t count) {
    GifPath *p = &gif_paths[path];
    size_t i = 0;

    while (i < count) {
        if (p->left == 0) {
            uint64_t tag = qwords[i * 2];
            uint32_t nloop = (uint32_t)(tag & 0x7FFF);
            uint32_t n;

            p->eop = (int)((tag >> 15) & 1);
            p->flg = (uint32_t)((tag >> 58) & 3);
            p->nreg = (uint32_t)(tag >> 60);
            if (p->nreg == 0) {
                p->nreg = 16;
            }
            p->regs = qwords[i * 2 + 1];
            p->reg = 0;
            p->ad = 0;
            if (p->flg == 0) {
                p->left = nloop * p->nreg;
                for (n = 0; n < p->nreg; n++) {
                    if (((p->regs >> (n * 4)) & 0xF) == 0xE) {
                        p->ad = 1;
                    }
                }
            } else if (p->flg == 1) {
                p->left = (nloop * p->nreg + 1) / 2;
            } else {
                p->left = nloop;
            }
            p->in_packet = !(p->eop && p->left == 0);
            i++;
            continue;
        }
        if (p->ad) {
            /* the A+D qwords: the one that writes FINISH */
            if (((p->regs >> (p->reg * 4)) & 0xF) == 0xE && (qwords[i * 2 + 1] & 0xFF) == GS_REG_FINISH) {
                __atomic_fetch_or(EE_HW(volatile uint64_t *, 0x12001000), (uint64_t)GS_CSR_FINISH, __ATOMIC_RELEASE);
            }
            p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            p->left--;
            i++;
        } else {
            size_t n = count - i < p->left ? count - i : p->left;

            p->left -= (uint32_t)n;
            i += n;
        }
        if (p->left == 0 && p->eop) {
            p->in_packet = 0;
        }
    }
}

int port_gs_path_idle(int path) {
    return !gif_paths[path].in_packet;
}

void port_gs_gif(int path, const void *data, size_t bytes) {
    uint64_t start = port_options.stats ? port_ticks() : 0;

    read_gif(path, data, bytes / 16);
#ifdef KF4_HAVE_PARALLEL_GS
    if (backend) {
        port_gs_backend_gif(path, data, bytes);
    }
#endif
    if (port_options.stats) {
        port_stats.gif_ticks += port_ticks() - start;
    }
    if (path != 3 && !gif_paths[path].in_packet) {
        port_gif_path3_poll();
    }
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
       shown by the time it was read. So it goes into the controller's queue after the channels
       started so far (dmac.c). */
    head = __atomic_load_n(&queue_head, __ATOMIC_RELAXED);
    while (head - __atomic_load_n(&queue_tail, __ATOMIC_ACQUIRE) >= VSYNC_QUEUE) {
        port_dmac_wake(); /* the GS is a whole queue behind: let it catch up */
    }
    request = &queue[head % VSYNC_QUEUE];
    memcpy(request->privileged, ee_hw_ptr(0x12000000), sizeof(request->privileged));
    request->vblank = port_vblank_count();
    request->field = (int)((*(volatile uint64_t *)ee_hw_ptr(0x12001000) >> 13) & 1);
    __atomic_store_n(&queue_head, head + 1, __ATOMIC_RELEASE);
    port_dmac_post_vsync();
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
    uint64_t start = port_options.stats ? port_ticks() : 0;
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
    if (port_options.stats) {
        port_stats.vsync_ticks += port_ticks() - start;
    }
    __atomic_store_n(&queue_tail, tail + 1, __ATOMIC_RELEASE);
}
