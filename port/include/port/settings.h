/* The player's settings (port/overlay/settings.c): what the overlay (F1) changes, kept in
   settings.json in the user's data folder. Options on the command line override them for that run.
   A run that is headless or plays a pad script (--input) reads no settings.json, so tests stay the
   same whatever the player has chosen. */
#ifndef PORT_SETTINGS_H
#define PORT_SETTINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASPECT_4_3 0     /* the TV's 4:3, whatever the window's shape */
#define ASPECT_STRETCH 1 /* the whole window */

typedef struct {
    /* Display */
    int window_height;     /* the window's height in pixels; 896 by default */
    int window_width;      /* its width in pixels (a monitor's size: 2560 x 1600, 3440 x 1440...), or 0 to follow
                              the picture's shape (4:3, or 16:9 with widescreen); 0 by default */
    int fullscreen;        /* borderless, on the whole screen */
    int aspect;            /* ASPECT_ */
    int vsync;             /* wait for the screen's refresh to show each picture */
    int smooth;            /* scale the picture with bilinear filtering (else nearest) */
    int deinterlace;       /* the four ways the PC picture differs from the GS's (port_options) */
    int progressive;
    int anti_blur;
    int adapt_width;
    int widescreen;        /* the opt-in 16:9 picture (port_options.widescreen) */
    int widescreen_hud_centred; /* with it, the HUD in the 4:3 middle instead of the 16:9 corner */
    int renderer;          /* who draws, from the next start: PORT_RENDERER_ (port_options.renderer) */
    int native_scale;      /* the native GS's resolution, 1 to 4 times the PS2's (port_options.native_scale) */
    /* Audio */
    int volume;            /* 0 to 100 */
    int mute;
    /* Controls (the bindings are port/sdk/controls.c's) */
    char layout[16];       /* "ps2" or "verdite" */
    /* Mods */
    int myus_necklace;     /* the effect of Myu's Necklace, always (port_options.myus_necklace) */
    /* The overlay */
    int pause_when_open;
    int show_fps;
    float ui_scale;        /* the overlay's size, 0.75 to 2 */
    int save_slot;         /* the slot the Save states page has chosen */
    int quick_slot;        /* the slot of the quick save and quick load keys (port/include/port/quicksave.h) */
} PortSettings;
extern PortSettings port_settings;

/* (main) Reads settings.json, unless the run is headless or plays a pad script, and puts its values
   into port_options where the command line left them alone. Writes it back at exit if changed. */
void port_settings_load(void);
/* 1 when settings.json was read (a run with a window and no pad script) */
int port_settings_active(void);
/* Something changed: written a second after the last change, and at exit */
void port_settings_changed(void);
/* (once a vblank, the game's thread) writes settings.json if it changed a while ago */
void port_settings_poll(void);
void port_settings_save(void);
/* The user's data folder: KF4_DATA_DIR, else %APPDATA%\holyland on Windows, $XDG_CONFIG_HOME/holyland
   (or ~/.config/holyland) elsewhere. Made when first written to. */
const char *port_data_dir(void);
/* settings.json's path */
const char *port_settings_path(void);
/* Puts the audio settings into the sound output (port_audio_set_volume) */
void port_settings_apply_audio(void);
/* Puts the display settings into the window: its size, full screen, vsync (port/platform/video.c) */
void port_video_apply_settings(void);

#ifdef __cplusplus
}
#endif

#endif
