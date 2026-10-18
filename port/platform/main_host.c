/* The PC program's entry: reads the command line, sets up the host side, then runs the game's own
   main.

     kf4 --disc <image.iso or folder> [options]
       --disc PATH       the user's own disc: an ISO image or the folder it was extracted to
                         (or KF4_DISC in the environment)
       --fast            do not hold the game to 59.94 vblanks a second
       --frames N        stop after N vblanks
       --input FILE      press the buttons a pad script says (port/sdk/libpad.c) instead of SDL's
       --log FILE        write the game's state each vblank, and what happens between ("-": stderr)
       --clock DATE      the disc drive's clock, YYYY-MM-DDTHH:MM:SS (Japan's time), not the host's
       --headless        no SDL at all: no window, and the GS draws only for --dump-frames
       --no-gs           draw nothing (VIF1 and VU1 still run)
       --no-deinterlace, --no-progressive, --no-anti-blur, --no-adapt-width
                         turn off one of the ways the PC picture differs from the GS's raw output
       --ps2-scanout     turn them all off
       --dump-frames N,N write the picture shown at these vblanks as PNGs
       --dump-dir DIR    where --dump-frames writes (the current directory by default)
       --host-dir DIR    where host0: files go (the current directory by default)
       --stats           at exit, print the time the DMA controller's thread spent in VU1, in
                         handing GIF packets to the GS and in vsyncs, and VU1's runs and instructions
       --audio-dump FILE write the sound as a 48 kHz WAV file
       --no-audio        play no sound (it is still made, for --audio-dump)
       --controls NAME   the keyboard and mouse layout: ps2 (the default) or verdite (mouse look,
                         port/sdk/controls.c) */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/audio.h"
#include "port/host.h"
#include "port/platform.h"

PortOptions port_options;
PortStats port_stats;

static FILE *log_file;

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s --disc <image.iso|folder> [--fast] [--frames N] [--input FILE] [--log FILE|-]\n"
            "          [--clock YYYY-MM-DDTHH:MM:SS] [--headless] [--no-gs] [--ps2-scanout] [--host-dir DIR]\n"
            "          [--dump-frames N,N,...] [--dump-dir DIR] [--stats]\n"
            "          [--audio-dump FILE] [--no-audio] [--controls ps2|verdite]\n",
            program);
    exit(2);
}

static void parse_clock(const char *text) {
    int *f = port_options.clock_fields;

    if (sscanf(text, "%d-%d-%dT%d:%d:%d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5]) != 6 || f[0] < 2000 ||
        f[0] > 2099 || f[1] < 1 || f[1] > 12 || f[2] < 1 || f[2] > 31 || f[3] > 23 || f[4] > 59 || f[5] > 59) {
        fprintf(stderr, "--clock wants YYYY-MM-DDTHH:MM:SS (2000 to 2099), not %s\n", text);
        exit(2);
    }
    port_options.clock_fixed = 1;
}

static void parse_options(int argc, char **argv) {
    int i;

    port_options.disc = getenv("KF4_DISC");
    port_options.deinterlace = port_options.progressive = port_options.anti_blur = port_options.adapt_width = 1;
    for (i = 1; i < argc; i++) {
        const char *option = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;

        if (strcmp(option, "--no-audio") == 0) {
            port_options.no_audio = 1;
            continue;
        }
        if (strcmp(option, "--fast") == 0) {
            port_options.fast = 1;
            continue;
        }
        if (strcmp(option, "--headless") == 0) {
            port_options.headless = 1;
            continue;
        }
        if (strcmp(option, "--no-gs") == 0) {
            port_options.no_gs = 1;
            continue;
        }
        if (strcmp(option, "--ps2-scanout") == 0) {
            port_options.deinterlace = port_options.progressive = port_options.anti_blur = port_options.adapt_width = 0;
            continue;
        }
        if (strcmp(option, "--no-deinterlace") == 0) {
            port_options.deinterlace = 0;
            continue;
        }
        if (strcmp(option, "--no-progressive") == 0) {
            port_options.progressive = 0;
            continue;
        }
        if (strcmp(option, "--no-anti-blur") == 0) {
            port_options.anti_blur = 0;
            continue;
        }
        if (strcmp(option, "--no-adapt-width") == 0) {
            port_options.adapt_width = 0;
            continue;
        }
        if (strcmp(option, "--stats") == 0) {
            port_options.stats = 1;
            continue;
        }
        if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0 || value == NULL) {
            usage(argv[0]);
        }
        i++;
        if (strcmp(option, "--disc") == 0) {
            port_options.disc = value;
        } else if (strcmp(option, "--frames") == 0) {
            port_options.frames = strtoll(value, NULL, 0);
        } else if (strcmp(option, "--input") == 0) {
            port_options.input = value;
        } else if (strcmp(option, "--log") == 0) {
            port_options.log = value;
        } else if (strcmp(option, "--clock") == 0) {
            parse_clock(value);
        } else if (strcmp(option, "--host-dir") == 0) {
            port_options.host_dir = value;
        } else if (strcmp(option, "--dump-frames") == 0) {
            port_options.dump_frames = value;
        } else if (strcmp(option, "--audio-dump") == 0) {
            port_options.audio_dump = value;
        } else if (strcmp(option, "--controls") == 0) {
            port_options.controls = value;
        } else if (strcmp(option, "--dump-dir") == 0) {
            port_options.dump_dir = value;
        } else {
            usage(argv[0]);
        }
    }
    if (port_options.disc == NULL) {
        fprintf(stderr, "kf4 needs your own King's Field IV disc: --disc <image.iso or folder> (or KF4_DISC)\n");
        usage(argv[0]);
    }
}

/* ---- The log ---------------------------------------------------------------------------------- */
int port_log_enabled(void) {
    return log_file != NULL;
}

void port_log(const char *fmt, ...) {
    va_list args;

    if (log_file == NULL) {
        return;
    }
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fputc('\n', log_file);
}

void port_event(const char *fmt, ...) {
    va_list args;

    if (log_file == NULL) {
        return;
    }
    fprintf(log_file, "%llu event: ", (unsigned long long)port_vblank_count());
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fputc('\n', log_file);
}

static void open_log(void) {
    if (port_options.log == NULL) {
        return;
    }
    if (strcmp(port_options.log, "-") == 0) {
        log_file = stderr;
        return;
    }
    log_file = fopen(port_options.log, "w");
    if (log_file == NULL) {
        fprintf(stderr, "cannot write the log %s\n", port_options.log);
        exit(2);
    }
    setvbuf(log_file, NULL, _IOLBF, 0); /* whole lines, so a fatal error leaves the log up to it */
}

static void close_log(void) {
    if (log_file != NULL && log_file != stderr) {
        fclose(log_file);
    }
    log_file = NULL;
}

/* ---- --stats ---------------------------------------------------------------------------------- */
static double start_seconds;

static void drained(void *unused) {
    (void)unused;
}

static void print_stats(void) {
    double per_second = port_ticks_per_second();
    double vu1 = (double)port_stats.vu1_ticks / per_second;

    /* the controller's thread finishes what the game started first, so the counts are the same
       every run */
    port_dmac_call(drained, NULL);
    fprintf(stderr, "[stats] wall %.3f s\n", port_wall_seconds() - start_seconds);
    fprintf(stderr, "[stats] VU1 %.3f s: %llu runs, %llu instructions (%.2f ns each)\n", vu1,
            (unsigned long long)port_stats.vu1_runs, (unsigned long long)port_stats.vu1_insns,
            port_stats.vu1_insns ? vu1 * 1e9 / (double)port_stats.vu1_insns : 0.0);
    fprintf(stderr, "[stats] GIF hand-off %.3f s, vsync %.3f s\n", (double)port_stats.gif_ticks / per_second,
            (double)port_stats.vsync_ticks / per_second);
    fprintf(stderr, "[stats] VU1 cycles %llu, PATH1 hash %016llx\n",
            (unsigned long long)port_stats.vu1_cycles, (unsigned long long)port_stats.path1_hash);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    parse_options(argc, argv);
    start_seconds = port_wall_seconds();
    open_log();
    atexit(close_log);
    if (port_options.stats) {
        atexit(print_stats);
    }
    port_memory_init();
    port_address_init();
    port_disc_open(port_options.disc);
    port_kernel_init();
    port_dmac_start();
    port_gs_init();
    port_pad_init();
    port_video_init();
    port_audio_init();
    fprintf(stderr, "[port] game RAM at %p, running the game\n", (void *)ee_ram);
    return kf4_game_main();
}
