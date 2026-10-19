/* The settings overlay (port/overlay/overlay.cpp): Dear ImGui over the game's picture, F1. It exists
   only with a window (SDL2), and never in a headless run or with a pad script (--input), so tests
   stay the same; KF4_OVERLAY_SHOT is the exception, for pictures of it (docs/port/README.md). */
#ifndef PORT_OVERLAY_H
#define PORT_OVERLAY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's state for the Debug page (game/state_log.c) */
typedef struct {
    unsigned frame;     /* the game's frame counter */
    int menu;           /* main_menu_state */
    unsigned status;    /* the game-status flag word */
    int map_slot;       /* the map slot in use, its map's tag, and whether it is loaded */
    int map_tag;
    int map_loaded;
    float x, y, z;      /* the player's position and heading */
    float heading;
    int hp, hp_max, mp, mp_max;
    int level, gold;
} PortGameReadout;
void kf4_game_readout(PortGameReadout *out);

#ifdef KF4_HAVE_SDL
/* (port/platform/video.c) after the window and its renderer are made */
void port_overlay_init(void *sdl_window, void *sdl_renderer);
/* (port/sdk/libpad.c) each SDL event: 1 when the overlay took it, and the game must not see it */
int port_overlay_event(const void *sdl_event);
/* 1 when the overlay draws something this vblank (open, a message, the frame rate, a test picture) */
int port_overlay_visible(void);
/* (port/platform/video.c) draws it over the picture, before the window is presented */
void port_overlay_draw(void);
/* 1 while it is open: the keyboard, mouse and controller go to it, not to the game */
int port_overlay_open(void);
/* The game holds still while the overlay is open ("Pause the game while the overlay is open") */
int port_overlay_paused(void);
/* (port/sdk/controls.c) a save state was asked for by its key: shows port_state_message() a while */
void port_overlay_notify_state(void);
#else
static inline int port_overlay_open(void) {
    return 0;
}
static inline int port_overlay_paused(void) {
    return 0;
}
static inline void port_overlay_notify_state(void) {
}
#endif

#ifdef __cplusplus
}
#endif

#endif
