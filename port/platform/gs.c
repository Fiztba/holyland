/* The GS: the GIF's three paths and the display, on the DMA controller's thread.

   Packets come from VU1's XGKICK (PATH1, port/platform/vif1.c), VIF1's DIRECT (PATH2) and DMA
   channel 2 (PATH3, port/platform/dmac.c), and go to the renderer --renderer chose: the backend in
   port/gs/gs_parallel.cpp (paraLLEl-GS, the default), the native GS (port/gs/native/, step 3's), or
   both, whose pictures are then compared (port_renderer_diff). At each vblank the kernel snapshots the display registers (PMODE..BGCOLOR in the
   register page) and queues them here; the controller's thread finishes the channels started before
   it, then has the backend scan the frame out, and hands the picture to the window
   (port/platform/video.c) and to --dump-frames.

   Without the backend (no Vulkan, or a --headless run that neither dumps nor captures) the packets are read all
   the same, so VIF1 and VU1 run, and then dropped. What the game does never depends on it: the
   picture is all the GS gives back, apart from the textures sceGsExecStoreImage reads, which the
   game only sends to the GS again. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/debug.h"
#include "port/host.h"
#include "port/platform.h"
#include "port/savestate.h"
#include "port/settings.h"

#ifdef KF4_HAVE_PARALLEL_GS
int port_gs_backend_open(void);
size_t port_gs_backend_state_size(void);
void port_gs_backend_state(PortStateIo *io);
void port_gs_backend_gif(int path, const void *data, size_t bytes);
void port_gs_backend_read_fifo(void *dst, uint32_t qwords);
int port_gs_backend_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);
void *port_gs_backend_context(void);
#include "gsn.h"
#endif

#define VSYNC_QUEUE 16

typedef struct {
    uint64_t privileged[0xF0 / 8];
    uint64_t vblank;
    int field;
} VsyncRequest;

static int backend;     /* a renderer is up */
static int have_gs;     /* paraLLEl-GS is */
static int have_native; /* the native GS is */
PortRendererDiff port_renderer_diff;
static VsyncRequest queue[VSYNC_QUEUE];
static volatile uint64_t queue_head; /* written by the game thread */
static volatile uint64_t queue_tail; /* written by the controller's thread */
static PortGsFrame frame;
static PortGsFrame native_frame; /* --renderer both: the native GS's picture, beside paraLLEl-GS's */

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
    if (port_options.renderer != PORT_RENDERER_NATIVE) {
        have_gs = port_gs_backend_open();
    }
    if (port_options.renderer != PORT_RENDERER_GS) {
        /* a Vulkan device of its own, beside paraLLEl-GS's (whose context has features on that the
           native GS does not want, and whose threads submit to the queue unlocked by the other device) */
        have_native = gsn_open(NULL);
        if (have_native) {
            char path[1100];

            gsn_set_scale(port_options.native_scale);
            gsn_set_presenter(port_video_submit);
            snprintf(path, sizeof(path), "%s/native_gs_pipelines.bin", port_data_dir());
            gsn_pipeline_cache(path);
        }
    }
    backend = have_gs || have_native;
#endif
}

void port_gs_init(void) {
    parse_dumps();
    if (port_options.no_gs || (port_options.headless && dump_count == 0 && !port_debug_wants_gs())) {
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
#define GS_REG_BITBLTBUF 0x50
#define GS_REG_TRXREG 0x52
#define GS_REG_TRXDIR 0x53
#define GS_REG_PRIM 0x00
#define GS_REG_XYZF2 0x04
#define GS_REG_XYZ2 0x05
#define GS_REG_XYZF3 0x0C
#define GS_REG_XYZ3 0x0D
#define GS_REG_XYOFFSET_1 0x18
#define GS_REG_TEX0_1 0x06
#define GS_REG_FRAME_1 0x4C
#define GS_REG_FRAME_2 0x4D
#define GS_REG_TEX0_2 0x07

typedef struct {
    uint32_t left;   /* qwords of the current tag's data still to come */
    uint32_t flg;    /* its FLG: 0 PACKED, 1 REGLIST, 2 and 3 IMAGE */
    uint32_t nreg;   /* its NREG (1..16) */
    uint64_t regs;   /* its REGS */
    uint32_t reg;    /* PACKED: the register of the next qword */
    int ad;          /* PACKED: one of the registers is A+D */
    int eop;         /* the current tag ends its packet */
    int in_packet;   /* between a packet's first tag and its end */
    /* --widescreen (below): read only while it is on */
    uint32_t prim;   /* PRIM as this path last set it */
    uint32_t tbp[2]; /* TEX0_1's and TEX0_2's TBP0, as this path last set them */
    uint32_t dwords; /* REGLIST: the register writes still to come */
    int squeezing;   /* the current tag is read register by register, to be squeezed */
    int widescreen;  /* PORT_WIDESCREEN_: what the path's drawing is, as the game last said */
} GifPath;

static GifPath gif_paths[4];

/* A host-to-local transfer (TRXDIR 0) takes its pixels from IMAGE packets, maybe across several:
   the bits still to come, so that a save state waits until none are (the GS's own count cannot be
   saved). BITBLTBUF and TRXREG as last written say how many a transfer takes. */
static uint64_t image_bitbltbuf;
static uint64_t image_trxreg;
static uint64_t image_bits_left;

static uint32_t psm_bits(uint32_t psm) {
    switch (psm) {
    case 0x01: /* PSMCT24 */
    case 0x31: /* PSMZ24 */
        return 24;
    case 0x02: /* PSMCT16 */
    case 0x0A: /* PSMCT16S */
    case 0x32: /* PSMZ16 */
    case 0x3A: /* PSMZ16S */
        return 16;
    case 0x13: /* PSMT8 */
        return 8;
    case 0x14: /* PSMT4 */
        return 4;
    default: /* the 32-bit formats, PSMT8H, PSMT4HL, PSMT4HH */
        return 32;
    }
}

/* --widescreen (below): the GS addresses that hold a picture of the frame, kept from the packets
   whether the switch is on or not: what was drawn into (FRAME), and where a local-to-local transfer
   or a 640-wide upload (the frame the game read back) went. Any other upload there makes it
   a texture again. Each is its TBP + 1 (0: none). */
#define FRAME_PICTURES 16
static uint32_t frame_pictures[FRAME_PICTURES];
static uint32_t frame_picture_next;

static void frame_picture(uint32_t tbp, int is_picture) {
    int i;

    for (i = 0; i < FRAME_PICTURES; i++) {
        if (frame_pictures[i] == tbp + 1) {
            if (!is_picture) {
                frame_pictures[i] = 0;
            }
            return;
        }
    }
    if (is_picture) {
        frame_pictures[frame_picture_next++ % FRAME_PICTURES] = tbp + 1;
    }
}

static void image_register(uint32_t reg, uint64_t value) {
    if (reg == GS_REG_BITBLTBUF) {
        image_bitbltbuf = value;
    } else if (reg == GS_REG_TRXREG) {
        image_trxreg = value;
    } else if (reg == GS_REG_TRXDIR) {
        if ((value & 3) != 1) {
            frame_picture((uint32_t)(image_bitbltbuf >> 32) & 0x3FFF, (value & 3) == 2 || (image_trxreg & 0xFFF) == 640);
        }
        if ((value & 3) == 0) {
            image_bits_left = (image_trxreg & 0xFFF) * ((image_trxreg >> 32) & 0xFFF) *
                              psm_bits((uint32_t)(image_bitbltbuf >> 56) & 0x3F);
        } else {
            image_bits_left = 0;
        }
    }
}

static void image_data(size_t qwords) {
    uint64_t bits = (uint64_t)qwords * 128;

    image_bits_left = bits < image_bits_left ? image_bits_left - bits : 0;
}

/* ---- Widescreen ------------------------------------------------------------------------------
   With port_widescreen() on, the game draws its 3D world with a 16:9 field of view into the same
   640x448 frame, and the window shows that frame at 16:9. Its 2D was laid out for 4:3, so it is
   squeezed here to three quarters of its width, as the vertices go by, which keeps its shape:
   - into the middle of the frame (PORT_WIDESCREEN_2D): the menus, the title, the message boxes;
   - or, for the HUD of the top left, against the frame's left edge, unless the player wants it
     centred (PORT_WIDESCREEN_HUD_LEFT, port_options.widescreen_hud_centred).
   What the packet chain (PATH1 and PATH2) draws is what the game last said with an A+D write to
   PORT_GS_REG_WIDESCREEN (game/widescreen.c; sent only while the switch is on); PATH3 carries nothing
   but 2D (the menus and the title), so it is squeezed unless told otherwise.
   Left as they are:
   - the 3D world (PATH1, and the triangles the game cuts up on the CPU, PATH2), projected wide;
   - anything drawn while the draw offset is not the 640x448 frame's (XYOFFSET_1 X 0x6C00): copies of
     the frame into a texture, scrolling textures, the 256x256 draw environment;
   - a vertex of an untextured primitive at or past the frame's left or right edge: full-screen fades
     and fills, and panels that reach the edge, stay reaching the 16:9 picture's edge;
   - a primitive textured with a picture of the frame (TBP0 0 or 0x1180, the two frame buffers, or
     0x2BC0, where the game copies the frame): the dimmed world behind the menus, the screen effects.
   A movie draws into a frame of its own, at a draw offset of 0: its pictures are squeezed about the
   middle of that frame, and its black clear, which reaches the edges, becomes the bars.
   With the switch off nothing here runs, and the packets go to the GS untouched. */
static uint32_t draw_offset_x = 0x6C00; /* XYOFFSET_1's X as last written, 12.4 */

int port_widescreen(void) {
    return port_options.widescreen;
}

/* A mark from the game: PATH1 and PATH2 come in the order of the one packet chain (VU1's packets and
   VIF1's DIRECT ones), so a mark in either goes for both. */
static void widescreen_marked(int path, int mode) {
    if (path == 3) {
        gif_paths[3].widescreen = mode;
    } else {
        gif_paths[1].widescreen = gif_paths[2].widescreen = mode;
    }
}

static int widescreen_mode(int path) {
    int mode = gif_paths[path].widescreen;

    if (mode == PORT_WIDESCREEN_3D && path == 3) {
        mode = PORT_WIDESCREEN_2D;
    }
    return mode;
}

static int is_frame_picture(uint32_t tbp) {
    int i;

    for (i = 0; i < FRAME_PICTURES; i++) {
        if (frame_pictures[i] == tbp + 1) {
            return 1;
        }
    }
    return 0;
}

/* the X of a vertex (12.4, in the low 16 bits of the register's data) for the path's mode */
static void squeeze_x(int path, uint8_t *data) {
    GifPath *p = &gif_paths[path];
    int mode = widescreen_mode(path);
    int x = data[0] | (data[1] << 8);
    int anchor = 0x8000;

    if (path == 3 && draw_offset_x == 0 && port_movie_playing()) {
        /* a movie (gfx/movie.c) draws its pictures into its own frame, with a draw offset of 0:
           squeezed about that frame's middle, between the bars of its black clear */
        anchor = 320 << 4;
    } else if (mode == PORT_WIDESCREEN_3D || (draw_offset_x >> 4) != 0x6C0) {
        return;
    }
    if (!(p->prim & 0x10) && (x <= 0x6C00 || x >= 0x9400)) {
        return;
    }
    if ((p->prim & 0x10) && is_frame_picture(p->tbp[(p->prim >> 9) & 1])) {
        return;
    }
    if (mode == PORT_WIDESCREEN_HUD_LEFT && !port_options.widescreen_hud_centred) {
        anchor = 0x6C00;
    }
    x = anchor + (x - anchor) * 3 / 4;
    data[0] = (uint8_t)x;
    data[1] = (uint8_t)(x >> 8);
}

/* A register write of a PACKED or REGLIST tag: PRIM and the vertices (only while squeezing). */
static void squeeze_register(int path, uint32_t reg, uint8_t *data) {
    switch (reg) {
    case GS_REG_PRIM:
        gif_paths[path].prim = (uint32_t)(data[0] | (data[1] << 8)) & 0x7FF;
        break;
    case GS_REG_TEX0_1:
    case GS_REG_TEX0_2:
        gif_paths[path].tbp[reg - GS_REG_TEX0_1] = (uint32_t)(data[0] | (data[1] << 8)) & 0x3FFF;
        break;
    case GS_REG_XYZF2:
    case GS_REG_XYZ2:
    case GS_REG_XYZF3:
    case GS_REG_XYZ3:
        squeeze_x(path, data);
        break;
    }
}

/* squeeze: the qwords may be changed (port_widescreen() is on, and they are a copy) */
static void read_gif(int path, uint64_t *qwords, size_t count, int squeeze) {
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
            p->squeezing = squeeze; /* not from the middle of a tag, when the switch has just been turned on */
            if (squeeze && ((tag >> 46) & 1)) {
                p->prim = (uint32_t)(tag >> 47) & 0x7FF; /* PRE */
            }
            if (p->flg == 0) {
                p->left = nloop * p->nreg;
                for (n = 0; n < p->nreg; n++) {
                    if (((p->regs >> (n * 4)) & 0xF) == 0xE) {
                        p->ad = 1;
                    }
                }
            } else if (p->flg == 1) {
                p->left = (nloop * p->nreg + 1) / 2;
                p->dwords = nloop * p->nreg;
            } else {
                p->left = nloop;
            }
            p->in_packet = !(p->eop && p->left == 0);
            i++;
            continue;
        }
        if (p->ad || (p->squeezing && squeeze && p->flg == 0)) {
            /* the A+D qwords: the one that writes FINISH */
            uint32_t descriptor = (uint32_t)(p->regs >> (p->reg * 4)) & 0xF;

            if (descriptor == 0xE) {
                uint32_t reg = (uint32_t)(qwords[i * 2 + 1] & 0xFF);

                if (reg == GS_REG_FINISH) {
                    __atomic_fetch_or(EE_HW(volatile uint64_t *, 0x12001000), (uint64_t)GS_CSR_FINISH, __ATOMIC_RELEASE);
                } else if (reg >= GS_REG_BITBLTBUF && reg <= GS_REG_TRXDIR) {
                    image_register(reg, qwords[i * 2]);
                } else if (reg == GS_REG_FRAME_1 || reg == GS_REG_FRAME_2) {
                    frame_picture((uint32_t)(qwords[i * 2] & 0x1FF) << 5, 1);
                } else if (reg == GS_REG_XYOFFSET_1) {
                    draw_offset_x = (uint32_t)(qwords[i * 2] & 0xFFFF);
                } else if (p->squeezing && squeeze) {
                    if (reg == PORT_GS_REG_WIDESCREEN) {
                        widescreen_marked(path, (int)(qwords[i * 2] & 0xFF));
                    } else {
                        squeeze_register(path, reg, (uint8_t *)&qwords[i * 2]);
                    }
                }
            } else if (p->squeezing && squeeze) {
                /* PACKED: XYZF2 and XYZ2 have X in the low 16 bits as the registers do; the rest
                   (XYZF3 and XYZ3 among them) are the register's data as it is */
                squeeze_register(path, descriptor, (uint8_t *)&qwords[i * 2]);
            }
            p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            p->left--;
            i++;
        } else if (p->squeezing && squeeze && p->flg == 1) {
            /* REGLIST: two registers a qword (the last qword's second is padding when the count is odd) */
            int half;

            for (half = 0; half < 2 && p->dwords != 0; half++, p->dwords--) {
                squeeze_register(path, (uint32_t)(p->regs >> (p->reg * 4)) & 0xF, (uint8_t *)&qwords[i * 2 + half]);
                p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            }
            p->left--;
            i++;
        } else {
            size_t n = count - i < p->left ? count - i : p->left;

            if (p->flg >= 2) {
                image_data(n);
            }
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

    if (port_options.widescreen) {
        /* a copy, squeezed (the game's packets are its own, and may be sent again) */
        static uint64_t *copy;
        static size_t copy_bytes;

        if (copy_bytes < bytes) {
            free(copy);
            copy_bytes = bytes * 2;
            copy = malloc(copy_bytes);
            if (copy == NULL) {
                PORT_FATAL("out of memory");
            }
        }
        memcpy(copy, data, bytes);
        data = copy;
        read_gif(path, copy, bytes / 16, 1);
    } else {
        read_gif(path, (uint64_t *)data, bytes / 16, 0);
    }
    if (port_gs_capture_recording) {
        port_gs_capture_gif(path, data, bytes);
    }
#ifdef KF4_HAVE_PARALLEL_GS
    if (have_gs) {
        port_gs_backend_gif(path, data, bytes);
    }
    if (have_native) {
        gsn_gif(path, data, bytes);
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
    if (port_gs_capture_recording) {
        port_gs_capture_read_fifo(qwords);
    }
#ifdef KF4_HAVE_PARALLEL_GS
    if (have_gs && have_native) {
        /* both read the same transfer; paraLLEl-GS's pixels are the ones the game gets */
        void *scratch = malloc((size_t)qwords * 16);

        if (scratch != NULL) {
            gsn_read_fifo(scratch, qwords);
            free(scratch);
        }
    } else if (have_native) {
        gsn_read_fifo(dst, qwords);
        return;
    }
    if (have_gs) {
        port_gs_backend_read_fifo(dst, qwords);
        return;
    }
#endif
    memset(dst, 0, (size_t)qwords * 16);
}

/* ---- Vblanks ---------------------------------------------------------------------------------- */
#ifdef KF4_HAVE_PARALLEL_GS
/* --renderer both: the two pictures, compared channel by channel */
static void compare_frames(const PortGsFrame *a, const PortGsFrame *b, uint64_t vblank) {
    PortRendererDiff *d = &port_renderer_diff;
    uint64_t sum = 0;
    uint32_t max = 0, pixels = 0;
    size_t i, n;

    if (a->width != b->width || a->height != b->height) {
        max = 255;
        pixels = a->width * a->height;
        n = 0;
    } else {
        n = (size_t)a->width * a->height;
    }
    for (i = 0; i < n; i++) {
        uint32_t p = a->rgba[i], q = b->rgba[i];
        uint32_t worst = 0;
        int c;

        if (p == q) {
            continue;
        }
        for (c = 0; c < 24; c += 8) {
            int e = (int)((p >> c) & 0xFF) - (int)((q >> c) & 0xFF);
            uint32_t m = (uint32_t)(e < 0 ? -e : e);

            sum += m;
            worst = m > worst ? m : worst;
        }
        max = worst > max ? worst : max;
        pixels += worst > 1;
    }
    d->frames++;
    d->frames_differ += pixels != 0;
    d->last_max = max;
    d->last_pixels = pixels;
    d->last_mean = n ? (double)sum / (double)(n * 3) : 255.0;
    if (max > d->worst_max) {
        d->worst_max = max;
        d->worst_vblank = vblank;
    }
}

/* --renderer both: the buffer the display shows, as each renderer drew it (the scanouts differ by
   their own conventions: paraLLEl-GS's crop of the TV picture and its weave of the fields) */
static void compare_buffers(VsyncRequest *request, int dump) {
    static PortGsFrame a, b;
    GsnRect rect;

    if (!gsn_display_rect(request->privileged, &rect)) {
        return;
    }
    if (a.rgba == NULL) {
        a.rgba = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
        b.rgba = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
    }
    a.width = b.width = rect.w;
    a.height = b.height = rect.h;
    port_gs_backend_read_rect(rect.bp, rect.bw, rect.psm, rect.x, rect.y, rect.w, rect.h, a.rgba);
    gsn_read_rect(&rect, b.rgba);
    compare_frames(&a, &b, request->vblank);
    if (dump) {
        char name[64];

        snprintf(name, sizeof(name), "frame_%06llu_gs_fb.png", (unsigned long long)request->vblank);
        port_video_dump_named(&a, name);
        snprintf(name, sizeof(name), "frame_%06llu_native_fb.png", (unsigned long long)request->vblank);
        port_video_dump_named(&b, name);
    }
}

/* A picture of a renderer's (or each's, with both) for the window and --dump-frames: 1 if one is shown */
static int present_frames(VsyncRequest *request, int shown, int dump) {
    int want = shown || dump;
    int got_gs = 0, got_native = 0;
    PortGsFrame *show;

    if (have_gs) {
        got_gs = port_gs_backend_vsync(request->privileged, request->field, want, &frame);
    }
    if (have_native) {
        if (!have_gs && shown && !dump) {
            /* the native GS shows the picture itself when it has it, and this thread goes on */
            return gsn_vsync(request->privileged, request->field, GSN_PRESENT, NULL);
        }
        got_native = gsn_vsync(request->privileged, request->field, want, &native_frame);
    }
    if (have_gs && have_native) {
        compare_buffers(request, dump);
    }
    if (have_gs && have_native) {
        show = port_options.renderer_shown == PORT_RENDERER_NATIVE ? &native_frame : &frame;
        if (dump && got_gs && got_native) {
            /* each picture, the one shown under the usual name */
            char name[64];

            port_video_dump(show, request->vblank);
            snprintf(name, sizeof(name), "frame_%06llu_gs.png", (unsigned long long)request->vblank);
            port_video_dump_named(&frame, name);
            snprintf(name, sizeof(name), "frame_%06llu_native.png", (unsigned long long)request->vblank);
            port_video_dump_named(&native_frame, name);
            dump = 0;
        }
        if (!(show == &frame ? got_gs : got_native)) {
            return 0;
        }
    } else if (have_native) {
        if (!got_native) {
            return 0;
        }
        show = &native_frame;
    } else {
        if (!got_gs) {
            return 0;
        }
        show = &frame;
    }
    if (dump) {
        port_video_dump(show, request->vblank);
    }
    if (shown) {
        port_video_submit(show);
    }
    return 1;
}
#endif

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
    if (port_gs_capture_armed) {
        port_gs_capture_note_vblank(request->vblank);
    }
    __atomic_store_n(&queue_head, head + 1, __ATOMIC_RELEASE);
    port_dmac_post_vsync();
}

void port_gs_set_native_scale(int scale) {
    port_options.native_scale = scale;
#ifdef KF4_HAVE_PARALLEL_GS
    if (have_native) {
        gsn_set_scale(scale);
    }
#endif
}

/* --stats: what the native GS did */
void port_gs_print_stats(void) {
#ifdef KF4_HAVE_PARALLEL_GS
    if (have_native) {
        const GsnStats *s = &gsn_stats;

        gsn_wait();
        fprintf(stderr,
                "[stats] native GS: %llu draws, %llu triangles, %llu render passes, %llu shader blends, %llu target "
                "copies, %llu write-backs; %llu textures decoded (%llu texels), %llu found again; %llu uploads (%.1f MB, %llu found in VRAM already, %llu copied in whole)\n",
                (unsigned long long)s->draws, (unsigned long long)s->primitives, (unsigned long long)s->passes,
                (unsigned long long)s->shader_blends, (unsigned long long)s->target_copies,
                (unsigned long long)s->writebacks, (unsigned long long)s->texture_decodes,
                (unsigned long long)s->texture_bytes, (unsigned long long)s->texture_reuses, (unsigned long long)s->uploads, (double)s->upload_bytes / 1048576.0,
                (unsigned long long)s->uploads_skipped,
                (unsigned long long)s->uploads_copied);
        fprintf(stderr,
                "[stats] native GS time: GIF %.3f s (draws %.3f, uploads %.3f: swizzle %.3f, targets %.3f; "
                "write-backs %.3f, decodes %.3f), vsync %.3f s\n",
                s->ns_gif / 1e9, s->ns_flush / 1e9, s->ns_upload / 1e9, s->ns_swizzle / 1e9, s->ns_sync / 1e9,
                s->ns_writeback / 1e9, s->ns_decode / 1e9, s->ns_vsync / 1e9);
        fprintf(stderr, "[stats] native GS draws: %.3f s working out their state, %.3f s recording them\n",
                s->ns_draw_setup / 1e9, s->ns_draw_record / 1e9);
        fprintf(stderr, "[stats] native GS holds: %llu targets, %llu textures, %llu decoded images (%llu MB), %llu uploads\n",
                (unsigned long long)s->targets, (unsigned long long)s->textures, (unsigned long long)s->pool_images,
                (unsigned long long)s->pool_mb, (unsigned long long)s->upload_copies);
    }
#endif
}

/* The GS's state as capture records (port/debug/gs_capture.c): paraLLEl-GS's, or the native GS's */
void port_gs_state_records(void (*emit)(uint32_t type, const void *data, size_t size)) {
#ifdef KF4_HAVE_PARALLEL_GS
    if (have_gs) {
        port_gs_backend_capture(emit);
    } else if (have_native) {
        gsn_capture(emit);
    }
#else
    (void)emit;
#endif
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
        native_frame.rgba = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
    }
    if (present_frames(request, shown, dump)) {
    } else if (dump) {
        fprintf(stderr, "[gs] vblank %llu: nothing is shown, no picture to dump\n", (unsigned long long)request->vblank);
    }
    if (port_gs_capture_armed) {
        port_gs_capture_vsync(request->privileged, request->field, request->vblank);
    }
#endif
    if (port_options.stats) {
        port_stats.vsync_ticks += port_ticks() - start;
    }
    __atomic_store_n(&queue_tail, tail + 1, __ATOMIC_RELEASE);
}

/* ---- Save states -------------------------------------------------------------------------------
   On the controller's thread, between GIF packets: the paths' places, and the GS's VRAM, registers
   and recent fields when there is a renderer, after their size (a state taken without one has none,
   and a run without one skips them). */
const char *port_gs_state_busy(void) {
    int path;

    for (path = 1; path < 4; path++) {
        if (gif_paths[path].in_packet) {
            return "a GIF packet is half sent";
        }
    }
    if (image_bits_left != 0) {
        return "an image transfer to the GS is half sent";
    }
    return NULL;
}

/* A renderer's part of a state saved by another (the state has the other's and not this one's): its
   VRAM, which both put first, so that the textures are there; the registers come back as the game
   next writes them */
static void take_other_vram(PortStateIo *io, uint64_t bytes, int into_native) {
    void *vram = bytes >= 4u << 20 ? malloc(4u << 20) : NULL;

    if (vram == NULL) {
        port_state_bytes(io, NULL, (size_t)bytes);
        return;
    }
    port_state_bytes(io, vram, 4u << 20);
    port_state_bytes(io, NULL, (size_t)(bytes - (4u << 20)));
#ifdef KF4_HAVE_PARALLEL_GS
    if (into_native) {
        gsn_restore(KF4GS_VRAM, vram, 4u << 20);
    } else {
        port_gs_backend_restore(KF4GS_VRAM, vram, 4u << 20);
    }
#else
    (void)into_native;
#endif
    free(vram);
}

void port_gs_state(PortStateIo *io) {
    uint64_t backend_bytes = 0;
    uint64_t native_bytes = 0;

    PORT_STATE(io, gif_paths);
    PORT_STATE(io, draw_offset_x);
    PORT_STATE(io, frame_pictures);
    PORT_STATE(io, frame_picture_next);
    PORT_STATE(io, image_bitbltbuf);
    PORT_STATE(io, image_trxreg);
    PORT_STATE(io, image_bits_left);
    /* paraLLEl-GS's state, then the native GS's: each only when it runs; a state loaded into the other
       renderer gives it the VRAM (take_other_vram) */
#ifdef KF4_HAVE_PARALLEL_GS
    if (have_gs) {
        backend_bytes = port_gs_backend_state_size();
    }
    if (have_native) {
        native_bytes = gsn_state_size();
    }
#endif
    PORT_STATE(io, backend_bytes);
    if (backend_bytes != 0) {
#ifdef KF4_HAVE_PARALLEL_GS
        if (have_gs) {
            port_gs_backend_state(io);
        } else if (have_native && io->loading) {
            take_other_vram(io, backend_bytes, 1);
            backend_bytes = 0; /* (the native GS's own, if any, is read below) */
        } else
#endif
        {
            port_state_bytes(io, NULL, (size_t)backend_bytes);
        }
    }
    PORT_STATE(io, native_bytes);
    if (native_bytes != 0) {
#ifdef KF4_HAVE_PARALLEL_GS
        if (have_native && native_bytes == gsn_state_size()) {
            gsn_state(io);
        } else if (have_gs && io->loading && !backend_bytes) {
            take_other_vram(io, native_bytes, 0);
        } else
#endif
        {
            port_state_bytes(io, NULL, (size_t)native_bytes);
        }
    }
}
