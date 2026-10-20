/* The host side of the PC port: what port/platform provides to the rest of port/ (not to the game). */
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* ---- Start-up -------------------------------------------------------------------------------- */

/* Allocates game RAM and resets the scratchpad, register pages and VU0 (port/platform/memory.c). */
void port_memory_init(void);

/* Gives the program's sections their EE addresses (port/platform/ee_address.c). */
void port_address_init(void);

/* The game's main (src/game/main.c, renamed by the build). */
int kf4_game_main(void);

/* What the command line asked for (port/platform/main_host.c). */
typedef struct {
    const char *disc;        /* --disc: the disc image (.iso) or the extracted disc's folder */
    const char *input;       /* --input: a pad script (port/sdk/libpad.c) instead of SDL */
    const char *log;         /* --log: the per-frame state log ("-" for stderr) */
    const char *host_dir;    /* --host-dir: where host0: files go (default: the current directory) */
    long long frames;        /* --frames: stop after this many vblanks (0: run on) */
    int fast;                /* --fast: do not hold the game to 59.94 vblanks a second */
    int headless;            /* --headless: no SDL at all, and no window */
    int no_gs;               /* --no-gs: draw nothing (the packets are still read) */
    /* --renderer: who draws (port/platform/gs.c). PORT_RENDERER_GS, paraLLEl-GS, is the default;
       PORT_RENDERER_NATIVE is step 3's (port/gs/native/); PORT_RENDERER_BOTH runs the two side by side
       and compares their pictures, showing renderer_shown's. */
    int renderer;
    int renderer_shown;
    int renderer_forced;     /* --renderer: this run's, whatever settings.json says */
    int native_scale;        /* --native-scale: the native GS's render targets, 1 to 4 times the PS2's size */
    int native_scale_forced;
    /* --vu1: who runs the VU1 programs (0x000, 0x280, 0x5A0, 0x700; 0x276 passes packets through;
       port/platform/vu1.c, vu1_hle*.c). PORT_VU1_INTERP, the interpreter, is the default;
       PORT_VU1_HLE computes the same results directly, to the bit (faster); PORT_VU1_CHECK runs
       both and compares them, run by run. */
    int vu1;
    /* How the GS's output becomes a PC picture (port/gs/gs_parallel.cpp). Each is on by default and
       turned off by --no-<name>; --ps2-scanout turns them all off (the GS's raw output). */
    int deinterlace;         /* weave the two fields of an interlaced picture into one */
    int progressive;         /* a frame-mode interlaced picture is read whole, not a field at a time */
    int anti_blur;           /* undo the blur of a game that blends its two read circuits */
    int adapt_width;         /* the picture's width follows the game's, not the 640 of the TV */
    /* The opt-in 16:9 picture (port_widescreen(), port/platform/gs.c): off by default, turned on by
       --widescreen or settings.json, live from the overlay. */
    int widescreen;
    int widescreen_forced;   /* --widescreen: on for this run whatever settings.json says */
    int widescreen_hud_centred; /* the HUD stays in the 4:3 middle instead of the 16:9 corner */
    /* The opt-in mods (port/include/port/host.h): off by default, turned on by their switch or
       settings.json, live from the overlay. */
    int myus_necklace;       /* the effect of Myu's Necklace, always (port_myus_necklace()) */
    int myus_necklace_forced; /* --myus-necklace: on for this run whatever settings.json says */
    int invincible;          /* --invincible, or the Debug page: no HP lost (port_invincible()); never saved */
    const char *dump_frames; /* --dump-frames: the vblanks whose picture is written, "N,N,..." */
    const char *dump_dir;    /* --dump-dir: where they go (default: the current directory) */
    const char *controls;    /* --controls: the keyboard and mouse layout, ps2 (default) or verdite */
    const char *memcard;     /* --memcard: the folder that is the memory card in slot 1, or "none" */
    const char *record_input; /* --record-input: write the live pad as a pad script */
    int clock_fixed;         /* --clock: the disc drive's clock is fixed (clock_fields), not the host's */
    const char *audio_dump;  /* --audio-dump: write the sound as a WAV file */
    int no_audio;            /* --no-audio: no sound device (the sound is still made) */
    int clock_fields[6];     /* year, month, day, hour, minute, second */
    int stats;               /* --stats: print where the controller's time went at exit */
    const char *state_save_at; /* --state-save-at: VBLANK[:SLOT|:FILE],... (port/platform/savestate.c) */
    const char *state_load;    /* --state-load: [VBLANK:]SLOT|FILE */
    const char *quick_save_at; /* --quick-save-at: VBLANK[:SLOT|:FILE],... (port/platform/quicksave.c) */
    const char *quick_load;    /* --quick-load: [VBLANK:]SLOT|FILE */
    const char *teleport;      /* --teleport: [VBLANK:]MAP[:PLACE] (port/platform/teleport.c) */
} PortOptions;
extern PortOptions port_options;

/* --stats: what the DMA controller's thread spent its time on (port_ticks), counted only when
   port_options.stats is set; main_host.c prints it at exit. */
typedef struct {
    uint64_t vu1_ticks;   /* in VU1's run, the GIF hand-offs it made excluded */
    uint64_t gif_ticks;   /* handing GIF packets to the GS (all three paths) */
    uint64_t vsync_ticks; /* the GS's scanouts, with the picture to the window and --dump-frames */
    uint64_t vu1_runs;    /* MSCAL, MSCALF and MSCNT */
    uint64_t vu1_insns;   /* instruction pairs run (or that the HLE did the work of) */
    uint64_t vu1_hle_runs; /* of the runs, those --vu1 hle took */
    uint64_t vu1_hle_ticks; /* and their share of vu1_ticks */
    uint64_t vu1_cycles;  /* VU1's own clock (stalls included): two runs that timed the same agree */
    uint64_t path1_hash;  /* of every packet VU1's XGKICK sent: two runs that drew the same agree */
} PortStats;
extern PortStats port_stats;

/* ---- Fibers (port/platform/fiber.c) ---------------------------------------------------------
   Each EE thread is a fiber of the host's main thread: one runs at a time, and a switch happens
   only where the kernel makes one. */
typedef struct PortFiber PortFiber;

/* The calling host thread as a fiber (the EE's main thread). */
PortFiber *port_fiber_main(void);
/* A fiber that will run entry(arg) when first switched to. entry must not return. */
PortFiber *port_fiber_create(size_t stack_size, void (*entry)(void *), void *arg);
/* Frees a fiber that is not running. */
void port_fiber_delete(PortFiber *fiber);
/* Saves the running fiber in from and runs to. */
void port_fiber_switch(PortFiber *from, PortFiber *to);

/* ---- The EE's integer division (port/platform/int_divide.c) --------------------------------
   INT_MIN / -1 gives INT_MIN and remainder 0, as the EE's DIV does, where x86's IDIV traps; a division
   by zero says where it was before the program ends (the PS2 stops there too: ee-gcc's break 7). */
void port_int_divide_init(void);

/* ---- The EE's float unit -------------------------------------------------------------------
   The EE's FPU (COP1) rounds toward zero, reads denormals as zero and flushes results that would be
   one to zero. The EE threads' fibers run with the SSE unit set the same way (MXCSR, which each fiber
   keeps as its own), so the game's float C computes as on the PS2 (docs/port/RULES.md, "PS2 numbers").
   --fpu-round nearest leaves them in the host's IEEE mode (round to nearest, denormals), to compare.
   What is left different: the EE has no infinities or NaNs (an exponent of 255 is a number, a
   result too large is +-0x7FFFFFFF and a division by zero +-0x7FFFFFFF, where SSE gives +-FLT_MAX,
   +-inf or a NaN).
   Host code on the game's fibers that formats or parses numbers runs in the host's mode: the C
   library's printf and strtod round as MXCSR says (UCRT's do). */
#define PORT_MXCSR_EE 0xFFC0u   /* exceptions masked, round toward zero, FTZ, DAZ */
#define PORT_MXCSR_HOST 0x1F80u /* exceptions masked, round to nearest */
#define PORT_MXCSR_FLAGS 0x003Fu
extern int port_fpu_round_nearest; /* --fpu-round nearest */
/* The MXCSR an EE thread's fiber starts with. */
uint32_t port_ee_mxcsr(void);
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
/* Host code that must compute or format in the host's mode: begin returns what end puts back. */
static inline uint32_t port_host_float_begin(void) {
    uint32_t saved = _mm_getcsr();

    _mm_setcsr(PORT_MXCSR_HOST);
    return saved;
}
static inline void port_host_float_end(uint32_t saved) {
    _mm_setcsr(saved);
}
#else
static inline uint32_t port_host_float_begin(void) {
    return 0;
}
static inline void port_host_float_end(uint32_t saved) {
    (void)saved;
}
#endif

/* ---- The EE kernel and its clock (port/platform/kernel.c) -----------------------------------
   Time is the EE's bus clock (147.456 MHz), counted by the host: it moves only at vblank waits and
   at the calls a game polls in a loop, so the same input gives the same run. */
#define EE_BUS_HZ 147456000u
/* The kernel's first steps, before the game's main runs. */
void port_kernel_init(void);
/* Runs the game's main as the EE's main thread, on a fiber; does not return. */
int port_kernel_run(int (*game_main)(void));
/* The current EE time in bus cycles, and the vblanks so far. */
uint64_t port_ee_cycles(void);
uint64_t port_vblank_count(void);
/* Waits for the next vblank start (the EE's VSync): other threads run meanwhile. */
void port_vsync(void);
/* Lets time pass for a call the game polls in a loop (cycles of EE time). */
void port_kernel_spend(uint32_t cycles);
/* Runs the interrupt handlers that came due (the DMA controller's ends). Every kernel call does. */
void port_kernel_poll(void);

/* ---- The DMA controller (port/platform/dmac.c) ----------------------------------------------
   A host thread that plays the EE's DMA controller: a channel the game starts (STR set in its
   CHCR) is finished, and its STR cleared, as the hardware would. The transfers reach the GS in the
   order the game started them. */
void port_dmac_start(void);
/* (the game's thread, at every call into the host) Queues the channels the game has started since the
   last call, and wakes the controller: the game may be about to start one. */
void port_dmac_wake(void);
/* Waits until a channel's STR is clear. */
void port_dmac_wait(int channel);
/* (the kernel) The channels among `wanted` started since the last call, once their transfers are done:
   their end interrupts are due. */
uint32_t port_dmac_ends(uint32_t wanted);
/* (port_gs_post_vsync) Queues a vblank's scanout after the transfers started before it. */
void port_dmac_post_vsync(void);
/* (the controller) PATH3 may go on: it was unmasked, or a PATH1 or PATH2 packet ended. */
void port_gif_path3_poll(void);
/* The channel's CHCR as the game sees it, and transfers finished so far. */
volatile int *port_dmac_chcr(int channel);
uint64_t port_dmac_transfers(int channel);
/* Runs fn(arg) on the controller's thread, which owns the GS, and returns when it has. */
void port_dmac_call(void (*fn)(void *), void *arg);

/* ---- The IPU's input (port/platform/ipu.c) --------------------------------------------------
   DMA channel 4, which the controller's thread leaves alone: it moves on the game's thread, as the
   movie decoder (port/sdk/libmpeg.c) takes the stream. */
/* Sees what the game wrote into D4_CHCR: a start, or a stop (the kernel calls it at EIntr, which
   ends the movie player's writes). */
void port_ipu_dma_sync(void);
/* The next quadword channel 4 sends; 0 while it is stopped or its chain has ended. */
int port_ipu_read_qword(uint8_t qword[16]);
/* Sets IPU_BP for a decoder that holds `unused_bits` bits it has read and not used. */
void port_ipu_set_bp(int unused_bits);
/* 1 while a movie plays (port/sdk/libmpeg.c: a decoder exists), from any thread */
int port_movie_playing(void);

/* ---- VIF1 and VU1 (port/platform/vif1.c, port/platform/vu1.c) -------------------------------
   Run on the controller's thread as DMA channel 1 feeds them. */
void port_vif1_reset(void);
/* Takes VIF codes and their data, in 32-bit words (any split: a command may span two calls). */
void port_vif1_feed(const uint32_t *words, size_t count);
/* VIF1's MSKPATH3: 1 while PATH3 is masked. */
int port_vif1_path3_masked(void);
/* XGKICK: sends the GIF packet at a VU1 data address (in qwords) by PATH1. */
void port_gif_path1_kick(uint32_t address);
/* The bytes of the GIF packet XGKICK would send from address, into out (32 KB); returns its size. */
size_t port_gif_path1_read(uint32_t address, uint8_t *out);

void port_vu1_reset(void);
/* MPG wrote micro memory: decode it again. */
void port_vu1_micro_written(void);
uint8_t *port_vu1_micro(void);
uint8_t *port_vu1_data(void);
/* Runs a microprogram from an address (in instructions) to its end, with VIF1's TOP and ITOP. */
void port_vu1_start(uint32_t address, uint32_t top, uint32_t itop);
/* MSCNT: runs on from where the last one ended. */
void port_vu1_continue(uint32_t top, uint32_t itop);
/* --vu1 */
enum { PORT_VU1_INTERP, PORT_VU1_HLE, PORT_VU1_CHECK };
/* --vu1 check: the runs compared, and those where the HLE and the interpreter differed (each
   difference is told on stderr, the first few) */
extern uint64_t port_vu1_check_runs, port_vu1_check_differ;

/* ---- The GS (port/platform/gs.c, port/gs/gs_parallel.cpp) -----------------------------------
   Everything but port_gs_init and port_gs_post_vsync runs on the controller's thread. */
enum { PORT_RENDERER_GS = 0, PORT_RENDERER_NATIVE = 1, PORT_RENDERER_BOTH = 2 };
/* --renderer both: how the last pictures of the two compared (port/platform/gs.c) */
typedef struct {
    uint64_t frames;          /* pictures compared */
    uint64_t frames_differ;   /* of which some pixel differs by more than 1 in a channel */
    uint32_t last_max;        /* the last picture's largest difference in a channel */
    uint32_t last_pixels;     /* its pixels that differ by more than 1 */
    double last_mean;         /* its mean difference per channel */
    uint32_t worst_max;       /* the largest difference seen */
    uint64_t worst_vblank;    /* where */
} PortRendererDiff;
extern PortRendererDiff port_renderer_diff;
void port_gs_print_stats(void);
/* The native GS's scale (port_options.native_scale), from the next vblank */
void port_gs_set_native_scale(int scale);

#define PORT_GS_FRAME_PIXELS (2560u * 2048u) /* the native GS at 4 times the PS2's 640x448 */
typedef struct {
    uint32_t width, height;
    uint32_t *rgba; /* width * height pixels, R in the low byte */
} PortGsFrame;
/* Starts the renderer (after port_dmac_start), unless the run draws nothing. */
void port_gs_init(void);
/* 1 when there is a renderer. */
int port_gs_enabled(void);
/* A GIF packet by PATH1, 2 or 3, in whole qwords. */
void port_gs_gif(int path, const void *data, size_t bytes);
/* 1 when a path is between GIF packets (the GIF may hand the GS to another path). */
int port_gs_path_idle(int path);
/* Reads qwords of a local-to-host transfer (BITBLTBUF/TRXDIR 1) from the GS. */
void port_gs_read_fifo(void *dst, uint32_t qwords);
/* At vblank start (the game thread): queues the display registers for a scanout. */
void port_gs_post_vsync(void);
/* Waits until every queued vblank is scanned out (before the program stops). */
void port_gs_finish(void);
/* The controller's side: a vblank is queued; scan it out. */
int port_gs_vsync_pending(void);
void port_gs_run_vsync(void);

/* ---- The window (port/platform/video.c) -------------------------------------------------- */
void port_video_init(void);
/* (the controller's thread) the newest picture, for the window */
void port_video_submit(const PortGsFrame *frame);
/* (the controller's thread) writes the picture as <dump-dir>/frame_<vblank>.png */
void port_video_dump(const PortGsFrame *frame, uint64_t vblank);
/* the same, under its own name in --dump-dir */
void port_video_dump_named(const PortGsFrame *frame, const char *name);
/* writes RGBA pixels (R in the low byte) as an RGB PNG: 1 when it was written */
int port_png_write(const char *path, const uint32_t *rgba, uint32_t width, uint32_t height);
/* (the game thread, at vblank) shows the newest picture and keeps the window alive */
void port_video_present(void);

/* ---- The disc (port/platform/disc.c) --------------------------------------------------------
   The user's own disc, as an ISO 9660 image or as the folder it was extracted to, read by sector
   number as the drive reads it. */
#define DISC_SECTOR_SIZE 2048u
void port_disc_open(const char *path);
/* Finds a file by its ISO 9660 path ("\DATA\KF4.DAT;1", any case, the version optional):
   1 and its first sector and size, or 0. */
int port_disc_find(const char *path, uint32_t *lsn, uint32_t *size);
/* Reads sectors into buffer; 0 when they are past the end of the disc. */
int port_disc_read(uint32_t lsn, uint32_t sectors, void *buffer);
/* The name of the file a sector belongs to, for the log (NULL: none). */
const char *port_disc_name_at(uint32_t lsn, uint32_t *first_lsn);

/* ---- The log --------------------------------------------------------------------------------
   port_log writes one line to the state log (--log), port_event the same with the vblank count,
   for the things that happen between frames (files opened, movies, the memory card). */
void port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void port_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int port_log_enabled(void);

/* The game's state in one line (game/state_log.c), written to the log once a frame. */
void kf4_game_state(char *line, size_t size);

/* ---- The memory card (port/sdk/libmc.c) ----------------------------------------------------- */
/* Opens the card folder of --memcard (by default <host-dir>/memcard), making it if it is missing */
void port_mc_init(void);

/* ---- The pad (port/sdk/libpad.c) ------------------------------------------------------------ */
void port_pad_init(void);
/* Once a vblank: SDL's events. */
void port_pad_poll(void);

/* ---- Small host helpers (port/platform/host_time.c) --------------------------------------- */
/* Seconds since the program started, from the host's monotonic clock. */
double port_wall_seconds(void);
/* Sleeps for about the given seconds. */
void port_sleep(double seconds);
/* The host's fastest monotonic counter (for --stats), and its ticks a second. */
uint64_t port_ticks(void);
double port_ticks_per_second(void);

#endif /* PORT_PLATFORM_H */
