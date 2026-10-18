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
    /* How the GS's output becomes a PC picture (port/gs/gs_parallel.cpp). Each is on by default and
       turned off by --no-<name>; --ps2-scanout turns them all off (the GS's raw output). */
    int deinterlace;         /* weave the two fields of an interlaced picture into one */
    int progressive;         /* a frame-mode interlaced picture is read whole, not a field at a time */
    int anti_blur;           /* undo the blur of a game that blends its two read circuits */
    int adapt_width;         /* the picture's width follows the game's, not the 640 of the TV */
    const char *dump_frames; /* --dump-frames: the vblanks whose picture is written, "N,N,..." */
    const char *dump_dir;    /* --dump-dir: where they go (default: the current directory) */
    const char *controls;    /* --controls: the keyboard and mouse layout, ps2 (default) or verdite */
    int clock_fixed;         /* --clock: the disc drive's clock is fixed (clock_fields), not the host's */
    int clock_fields[6];     /* year, month, day, hour, minute, second */
} PortOptions;
extern PortOptions port_options;

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

/* ---- The EE kernel and its clock (port/platform/kernel.c) -----------------------------------
   Time is the EE's bus clock (147.456 MHz), counted by the host: it moves only at vblank waits and
   at the calls a game polls in a loop, so the same input gives the same run. */
#define EE_BUS_HZ 147456000u
/* The kernel's first steps, before the game's main runs. */
void port_kernel_init(void);
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

/* ---- VIF1 and VU1 (port/platform/vif1.c, port/platform/vu1.c) -------------------------------
   Run on the controller's thread as DMA channel 1 feeds them. */
void port_vif1_reset(void);
/* Takes VIF codes and their data, in 32-bit words (any split: a command may span two calls). */
void port_vif1_feed(const uint32_t *words, size_t count);
/* VIF1's MSKPATH3: 1 while PATH3 is masked. */
int port_vif1_path3_masked(void);
/* XGKICK: sends the GIF packet at a VU1 data address (in qwords) by PATH1. */
void port_gif_path1_kick(uint32_t address);

void port_vu1_reset(void);
/* MPG wrote micro memory: decode it again. */
void port_vu1_micro_written(void);
uint8_t *port_vu1_micro(void);
uint8_t *port_vu1_data(void);
/* Runs a microprogram from an address (in instructions) to its end, with VIF1's TOP and ITOP. */
void port_vu1_start(uint32_t address, uint32_t top, uint32_t itop);
/* MSCNT: runs on from where the last one ended. */
void port_vu1_continue(uint32_t top, uint32_t itop);

/* ---- The GS (port/platform/gs.c, port/gs/gs_parallel.cpp) -----------------------------------
   Everything but port_gs_init and port_gs_post_vsync runs on the controller's thread. */
#define PORT_GS_FRAME_PIXELS (1024u * 1024u)
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

/* ---- The pad (port/sdk/libpad.c) ------------------------------------------------------------ */
void port_pad_init(void);
/* Once a vblank: SDL's events. */
void port_pad_poll(void);

/* ---- Small host helpers (port/platform/host_time.c) --------------------------------------- */
/* Seconds since the program started, from the host's monotonic clock. */
double port_wall_seconds(void);
/* Sleeps for about the given seconds. */
void port_sleep(double seconds);

#endif /* PORT_PLATFORM_H */
