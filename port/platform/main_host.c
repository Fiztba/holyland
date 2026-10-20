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
       --headless        no SDL at all: no window, and the GS draws only for --dump-frames and
                         GS captures (--gs-capture-at, KF4_DEBUG_AT)
       --no-gs           draw nothing (VIF1 and VU1 still run)
       --renderer NAME   who draws: gs (paraLLEl-GS, the default), native (step 3's renderer,
                         port/gs/native/) or both (side by side, their pictures compared; the
                         window shows gs's, --renderer-shown native shows the other)
       --native-scale N  the native GS draws at N times the PS2's resolution (1 to 4; 1 by default)
       --vu1 NAME        who runs VU1's programs 0x000 and 0x280 (most of the 3D): interp (the
                         interpreter, the default), hle (the same results to the bit, computed
                         directly: faster; port/platform/vu1_hle.c) or check (both, compared run by run,
                         differences on stderr and counted by --stats)
       --no-deinterlace, --no-progressive, --no-anti-blur, --no-adapt-width
                         turn off one of the ways the PC picture differs from the GS's raw output
       --ps2-scanout     turn them all off
       --widescreen      the opt-in 16:9 picture: a wider view, the 2D kept at 4:3 (port/platform/gs.c)
       --widescreen-hud-centred
                         the same, with the HUD in the 4:3 middle instead of the 16:9 corner
       --myus-necklace   the opt-in mod: the effect of Myu's Necklace always, worn or not
       --invincible      debugging: the player loses no HP and does not die (the overlay's Debug page)
       --cheats LIST     debugging: once in play, spells, equipment, items and/or magic (comma-separated,
                         or all), as the Debug page's buttons do; spells1, spells2 or spells3 for the
                         spells at that level (3 otherwise) (port/include/port/cheats.h)
       --dump-frames N,N write the picture shown at these vblanks as PNGs
       --dump-dir DIR    where --dump-frames writes (the current directory by default)
       --host-dir DIR    where host0: files go (the current directory by default)
       --stats           at exit, print the time the DMA controller's thread spent in VU1, in
                         handing GIF packets to the GS and in vsyncs, and VU1's runs and instructions
       --audio-dump FILE write the sound as a 48 kHz WAV file
       --no-audio        play no sound (it is still made, for --audio-dump)
       --controls NAME   the keyboard and mouse layout: ps2 (the default) or verdite (mouse look,
                         port/sdk/controls.c)
       --memcard DIR     the folder that is the memory card in slot 1 (by default memcard in the
                         data folder, next to settings.json); "none" leaves the slot empty
       --record-input F  write what the pad and keyboard send as a pad script, to replay with --input
                         (the sound then follows the game's clock and mouse look is off, so it replays)
       --state-save-at VBLANK[:SLOT|:FILE],...
                         save a state at these vblanks (port/platform/savestate.c)
       --state-load [VBLANK:]SLOT|FILE
                         load a state before the game starts, or at that vblank
       --quick-save-at VBLANK[:SLOT|:FILE],...
                         take a quick save once in play from these vblanks (port/platform/quicksave.c)
       --quick-load [VBLANK:]SLOT|FILE,...
                         load a quick save on the title screen, or in play once the run reaches VBLANK;
                         several, in vblank order, are loaded one after another
       --teleport [VBLANK:]MAP[:PLACE]
                         once in play (from VBLANK), put the player in that map at its PLACE-th place
                         (0, the map's start, when left out; VBLANK:MAP:PLACE with a vblank), keeping
                         the game's progress, as the overlay's Debug page does (port/platform/teleport.c)
       --gs-capture-at VBLANK[:N],...
                         write a GS frame capture of N vblanks (2 when left out) from these vblanks,
                         for kf4-gsreplay (port/debug/gs_capture.c; F9 does the same)
       --fpu-round zero|nearest
                         how the game's float maths rounds: as the EE's FPU (zero, the default:
                         toward zero, no denormals) or as the PC's IEEE floats (nearest), to compare
                         (port/include/port/platform.h)
   With a window and no pad script, settings.json (port/overlay/settings.c, the overlay's F1) fills in
   what the command line does not say. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/audio.h"
#include "port/debug.h"
#include "port/host.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/savestate.h"
#include "port/quicksave.h"
#include "port/teleport.h"
#include "port/cheats.h"

static int parse_cheats(const char *list);
#include "port/sanitize.h"

#ifdef _WIN32
#include <windows.h>
/* A crash on Windows says where it was, which `llvm-addr2line -f -e kf4.exe ADDRESS` names (the
   program is linked at a fixed address) */
static LONG WINAPI crash_report(EXCEPTION_POINTERS *e) {
    fprintf(stderr, "[port] crashed: exception 0x%08lx at %p\n", (unsigned long)e->ExceptionRecord->ExceptionCode,
            e->ExceptionRecord->ExceptionAddress);
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2) {
        fprintf(stderr, "[port] (a %s of %p)\n", e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                (void *)e->ExceptionRecord->ExceptionInformation[1]);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

#if PORT_ASAN
/* ASan's defaults for this program (ASAN_OPTIONS overrides them): no fake stacks, which would keep
   locals outside the EE threads' stacks, where a save state does not see them; no leak check at exit */
const char *__asan_default_options(void) {
    return "detect_stack_use_after_return=0:detect_leaks=0";
}
#endif

PortOptions port_options;
PortStats port_stats;

static FILE *log_file;

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s --disc <image.iso|folder> [--fast] [--frames N] [--input FILE] [--log FILE|-]\n"
            "          [--clock YYYY-MM-DDTHH:MM:SS] [--headless] [--no-gs] [--ps2-scanout] [--host-dir DIR]\n"
            "          [--dump-frames N,N,...] [--dump-dir DIR] [--stats]\n"
            "          [--audio-dump FILE] [--no-audio] [--controls ps2|verdite] [--memcard DIR|none]\n"
            "          [--record-input FILE] [--state-save-at VBLANK[:SLOT|:FILE],...]\n"
            "          [--state-load [VBLANK:]SLOT|FILE] [--quick-save-at VBLANK[:SLOT|:FILE],...]\n"
            "          [--quick-load [VBLANK:]SLOT|FILE,...] [--widescreen] [--widescreen-hud-centred]\n"
            "          [--gs-capture-at VBLANK[:N],...] [--myus-necklace] [--invincible] [--cheats LIST] [--fpu-round zero|nearest]\n"
            "          [--teleport [VBLANK:]MAP[:PLACE]] [--renderer gs|native|both] [--renderer-shown gs|native]\n"
            "          [--native-scale 1-4] [--vu1 interp|hle|check]\n",
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

static const char *gs_capture_at; /* --gs-capture-at (port/debug/gs_capture.c) */

static void parse_options(int argc, char **argv) {
    int i;

    port_options.disc = getenv("KF4_DISC");
    port_options.deinterlace = port_options.progressive = port_options.anti_blur = port_options.adapt_width = 1;
    port_options.native_scale = 1;
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
        if (strcmp(option, "--widescreen") == 0 || strcmp(option, "--widescreen-hud-centred") == 0) {
            port_options.widescreen = port_options.widescreen_forced = 1;
            if (strcmp(option, "--widescreen-hud-centred") == 0) {
                port_options.widescreen_hud_centred = 1;
            }
            continue;
        }
        if (strcmp(option, "--invincible") == 0) {
            port_options.invincible = 1;
            continue;
        }
        if (strcmp(option, "--myus-necklace") == 0) {
            port_options.myus_necklace = port_options.myus_necklace_forced = 1;
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
        } else if (strcmp(option, "--renderer") == 0 || strcmp(option, "--renderer-shown") == 0) {
            int both = strcmp(option, "--renderer") == 0;
            int which = strcmp(value, "gs") == 0             ? PORT_RENDERER_GS
                        : strcmp(value, "native") == 0       ? PORT_RENDERER_NATIVE
                        : both && strcmp(value, "both") == 0 ? PORT_RENDERER_BOTH
                                                             : -1;
            if (which < 0) {
                fprintf(stderr, "%s wants gs, native%s, not %s\n", option, both ? " or both" : "", value);
                exit(2);
            }
            if (both) {
                port_options.renderer = which;
                port_options.renderer_forced = 1;
            } else {
                port_options.renderer_shown = which;
            }
        } else if (strcmp(option, "--native-scale") == 0) {
            port_options.native_scale = atoi(value);
            if (port_options.native_scale < 1 || port_options.native_scale > 4) {
                fprintf(stderr, "--native-scale wants 1 to 4, not %s\n", value);
                exit(2);
            }
            port_options.native_scale_forced = 1;
        } else if (strcmp(option, "--vu1") == 0) {
            if (strcmp(value, "interp") == 0) {
                port_options.vu1 = PORT_VU1_INTERP;
            } else if (strcmp(value, "hle") == 0) {
                port_options.vu1 = PORT_VU1_HLE;
            } else if (strcmp(value, "check") == 0) {
                port_options.vu1 = PORT_VU1_CHECK;
            } else {
                fprintf(stderr, "--vu1 wants interp, hle or check, not %s\n", value);
                exit(2);
            }
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
        } else if (strcmp(option, "--record-input") == 0) {
            port_options.record_input = value;
        } else if (strcmp(option, "--memcard") == 0) {
            port_options.memcard = value;
        } else if (strcmp(option, "--controls") == 0) {
            port_options.controls = value;
        } else if (strcmp(option, "--dump-dir") == 0) {
            port_options.dump_dir = value;
        } else if (strcmp(option, "--state-save-at") == 0) {
            port_options.state_save_at = value;
        } else if (strcmp(option, "--state-load") == 0) {
            port_options.state_load = value;
        } else if (strcmp(option, "--quick-save-at") == 0) {
            port_options.quick_save_at = value;
        } else if (strcmp(option, "--quick-load") == 0) {
            port_options.quick_load = value;
        } else if (strcmp(option, "--cheats") == 0) {
            if (!parse_cheats(value)) {
                usage(argv[0]);
            }
        } else if (strcmp(option, "--teleport") == 0) {
            port_options.teleport = value;
        } else if (strcmp(option, "--gs-capture-at") == 0) {
            gs_capture_at = value;
        } else if (strcmp(option, "--fpu-round") == 0) {
            if (strcmp(value, "zero") != 0 && strcmp(value, "nearest") != 0) {
                usage(argv[0]);
            }
            port_fpu_round_nearest = strcmp(value, "nearest") == 0;
        } else {
            usage(argv[0]);
        }
    }
    if (port_options.disc == NULL) {
        fprintf(stderr, "kf4 needs your own King's Field IV disc: --disc <image.iso or folder> (or KF4_DISC)\n");
        usage(argv[0]);
    }
}

int port_myus_necklace(void) {
    return port_options.myus_necklace != 0;
}

/* --cheats spells,equipment,items,magic or all: 0 for a word it does not know */
static int parse_cheats(const char *list) {
    static const struct {
        const char *name;
        int bits;
    } names[] = {{"spells", PORT_CHEAT_SPELLS}, {"spells1", PORT_CHEAT_SPELLS | 0x100}, {"spells2", PORT_CHEAT_SPELLS | 0x200},
                 {"spells3", PORT_CHEAT_SPELLS | 0x300}, {"equipment", PORT_CHEAT_EQUIPMENT}, {"items", PORT_CHEAT_ITEMS},
                 {"magic", PORT_CHEAT_MAGIC},
                 {"all", PORT_CHEAT_SPELLS | PORT_CHEAT_EQUIPMENT | PORT_CHEAT_ITEMS | PORT_CHEAT_MAGIC}};
    const char *at = list;

    while (*at != 0) {
        size_t length = strcspn(at, ",");
        size_t i;
        int found = 0;

        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strlen(names[i].name) == length && strncmp(at, names[i].name, length) == 0) {
                if (names[i].bits >> 8) {
                    port_cheat_set_spell_level(names[i].bits >> 8);
                }
                port_cheat_request(names[i].bits & 0xFF);
                found = 1;
            }
        }
        if (!found) {
            return 0;
        }
        at += length;
        if (*at == ',') {
            at++;
        }
    }
    return 1;
}

int port_invincible(void) {
    return port_options.invincible != 0;
}

/* ---- The log ---------------------------------------------------------------------------------- */
int port_log_enabled(void) {
    return log_file != NULL;
}

/* (both format in the host's float mode: they are called from the game's threads) */
void port_log(const char *fmt, ...) {
    va_list args;
    uint32_t mode;

    if (log_file == NULL) {
        return;
    }
    mode = port_host_float_begin();
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fputc('\n', log_file);
    port_host_float_end(mode);
}

void port_event(const char *fmt, ...) {
    va_list args;
    uint32_t mode = port_host_float_begin();

    va_start(args, fmt);
    port_debug_note_event(port_vblank_count(), fmt, args); /* F10's last events (port/debug/debug.c) */
    va_end(args);
    if (log_file != NULL) {
        fprintf(log_file, "%llu event: ", (unsigned long long)port_vblank_count());
        va_start(args, fmt);
        vfprintf(log_file, fmt, args);
        va_end(args);
        fputc('\n', log_file);
    }
    port_host_float_end(mode);
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
    uint32_t mode = port_host_float_begin(); /* (at exit, which the game's thread may call) */
    double per_second = port_ticks_per_second();
    double vu1 = (double)port_stats.vu1_ticks / per_second;

    /* the controller's thread finishes what the game started first, so the counts are the same
       every run */
    port_dmac_call(drained, NULL);
    fprintf(stderr, "[stats] wall %.3f s\n", port_wall_seconds() - start_seconds);
    port_gs_print_stats();
    if (port_options.renderer == PORT_RENDERER_BOTH) {
        const PortRendererDiff *d = &port_renderer_diff;

        fprintf(stderr, "[stats] renderers: %llu pictures compared, %llu differ (a channel by more than 1); "
                        "the largest difference %u at vblank %llu\n",
                (unsigned long long)d->frames, (unsigned long long)d->frames_differ, d->worst_max,
                (unsigned long long)d->worst_vblank);
    }
    fprintf(stderr, "[stats] VU1 %.3f s: %llu runs, %llu instructions (%.2f ns each)\n", vu1,
            (unsigned long long)port_stats.vu1_runs, (unsigned long long)port_stats.vu1_insns,
            port_stats.vu1_insns ? vu1 * 1e9 / (double)port_stats.vu1_insns : 0.0);
    if (port_options.vu1 == PORT_VU1_HLE) {
        fprintf(stderr, "[stats] VU1 HLE: %llu of the runs, %.3f s of the time\n",
                (unsigned long long)port_stats.vu1_hle_runs, (double)port_stats.vu1_hle_ticks / per_second);
    } else if (port_options.vu1 == PORT_VU1_CHECK) {
        fprintf(stderr, "[stats] VU1 check: %llu runs compared, %llu differ\n",
                (unsigned long long)port_vu1_check_runs, (unsigned long long)port_vu1_check_differ);
    }
    fprintf(stderr, "[stats] GIF hand-off %.3f s, vsync %.3f s\n", (double)port_stats.gif_ticks / per_second,
            (double)port_stats.vsync_ticks / per_second);
    fprintf(stderr, "[stats] VU1 cycles %llu, PATH1 hash %016llx\n",
            (unsigned long long)port_stats.vu1_cycles, (unsigned long long)port_stats.path1_hash);
    port_host_float_end(mode);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    parse_options(argc, argv);
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_report);
#endif
    port_int_divide_init(); /* INT_MIN / -1 as the EE gives it */
    port_settings_load(); /* settings.json, where the command line left an option alone */
    start_seconds = port_wall_seconds();
    open_log();
    atexit(close_log);
    if (port_options.stats) {
        atexit(print_stats);
    }
    port_memory_init();
    port_address_init();
    port_disc_open(port_options.disc);
    port_mc_init();
    port_kernel_init();
    port_dmac_start();
    port_debug_init(gs_capture_at); /* before the GS: a capture asked for makes a --headless run draw */
    port_gs_init();
    port_pad_init();
    port_video_init();
    port_audio_init();
    port_state_init();
    port_quick_init();
    port_teleport_init();
    fprintf(stderr, "[port] game RAM at %p, running the game\n", (void *)ee_ram);
    return port_kernel_run(kf4_game_main);
}
