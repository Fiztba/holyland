/* Save states (port/include/port/savestate.h). Not written yet: every request is refused. */
#include "port/savestate.h"

void port_state_request_save(int slot) {
    (void)slot;
}

void port_state_request_load(int slot) {
    (void)slot;
}

int port_state_exists(int slot) {
    (void)slot;
    return 0;
}

long long port_state_time(int slot) {
    (void)slot;
    return 0;
}

const char *port_state_message(void) {
    return "save states are not written yet";
}
