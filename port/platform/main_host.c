/* The PC program's entry: sets up the host side, then runs the game's own main. */
#include <stdio.h>

#include "port/host.h"
#include "port/platform.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    port_memory_init();
    port_address_init();
    fprintf(stderr, "[port] game RAM at %p, running the game\n", (void *)ee_ram);
    return kf4_game_main();
}
