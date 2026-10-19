/* Save states (port/platform/savestate.c): the whole running game written to a file and read back,
   for testing and debugging. A request is taken at the next safe point (a vblank, with every EE
   thread parked in the kernel), never in the middle of the game's code. */
#ifndef PORT_SAVESTATE_H
#define PORT_SAVESTATE_H

/* Asks for the state to be saved to, or loaded from, `slot` (0 is the quick slot) at the next safe
   point. Safe from any thread (the window's events, the settings overlay). */
void port_state_request_save(int slot);
void port_state_request_load(int slot);

/* Whether `slot` has a state, and when it was written (seconds since 1970, 0 if none). */
int port_state_exists(int slot);
long long port_state_time(int slot);

/* The last thing a request did, for the overlay and the log ("saved to slot 0", or why not). */
const char *port_state_message(void);

#endif
