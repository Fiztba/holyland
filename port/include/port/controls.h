/* PC controls (port/sdk/controls.c): the Verdite keyboard layout and mouse look, opt-in with
   --controls verdite. The default keeps the PS2-style layout of port/sdk/libpad.c. */
#ifndef PORT_CONTROLS_H
#define PORT_CONTROLS_H

/* Called by the game where the player's frame applies the turn and look (Player_Move), whether the
   player is in control (`gameplay`). Gives the mouse's turn and look since the last call, in
   radians, and returns 1; returns 0 and changes nothing when mouse look is off, the mouse is not
   captured, the player is not in control, or a pad script is playing (so runs stay deterministic). */
int port_mouse_look(int gameplay, float *yaw, float *pitch);

/* The port's side (port/sdk/libpad.c, port/platform/video.c) */
void port_controls_init(const char *layout); /* "ps2" (or NULL) or "verdite" */
int port_controls_verdite(void);             /* the Verdite layout is on (and no pad script plays) */
void port_controls_event(const void *sdl_event);
void port_controls_read(unsigned int *buttons);
void port_controls_draw(void *sdl_renderer, int x, int y, int width, int height);

#endif
