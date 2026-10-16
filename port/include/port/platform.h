/* The host side of the PC port: what port/platform provides to the rest of port/ (not to the game). */
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/* Allocates game RAM and resets the scratchpad, register pages and VU0 (port/platform/memory.c). */
void port_memory_init(void);

/* Gives the program's sections their EE addresses (port/platform/ee_address.c). */
void port_address_init(void);

/* The game's main (src/game/main.c, renamed by the build). */
int kf4_game_main(void);

#endif /* PORT_PLATFORM_H */
