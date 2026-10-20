/* Debug cheats (port/include/port/cheats.h): the requests and the message. The overlay and the game both
   run on the game's thread, so these need no locking. */
#include <stdio.h>
#include <string.h>

#include "port/cheats.h"

static int requested;
static int spell_level = 3;
static char message[160] = "";

void port_cheat_request(int cheats) {
    requested |= cheats;
    snprintf(message, sizeof(message), "asked for: taken once you are in play");
}

const char *port_cheat_message(void) {
    return message;
}

void port_cheat_set_spell_level(int level) {
    spell_level = level < 1 ? 1 : level > 3 ? 3 : level;
}

int port_cheat_spell_level(void) {
    return spell_level;
}

int port_cheat_take(void) {
    int cheats = requested;

    requested = 0;
    return cheats;
}

int port_cheat_pending(void) {
    return requested;
}

void port_cheat_set_message(const char *text) {
    if (strcmp(text, message) == 0) {
        return; /* (the wait's message comes each frame until play) */
    }
    snprintf(message, sizeof(message), "%s", text);
    fprintf(stderr, "[cheat] %s\n", message);
}
