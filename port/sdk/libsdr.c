/* libsdr, the EE end of libsd's remote calls (port/include/libsdr.h): each call goes to SDRDRV on the
   IOP side (port/audio/sdrdrv.c) with its arguments, and returns libsd's result. */
#include <stdarg.h>
#include <stdint.h>

#include "libsdr.h"
#include "port/audio.h"

int sceSdRemoteInit(void) {
    return 0;
}

int sceSdRemote(int wait, int command, ...) {
    uint32_t args[5] = {0};
    int count = port_sdrdrv_arg_count(command);
    va_list ap;
    int i;

    (void)wait; /* the call is done when it returns, whether the game waits or not */
    va_start(ap, command);
    for (i = 0; i < count; i++) {
        args[i] = va_arg(ap, uint32_t);
    }
    /* a block transfer from a start address has a fifth argument */
    if (count == 4 && (args[1] & 3) == 3) {
        args[4] = va_arg(ap, uint32_t);
    }
    va_end(ap);
    return port_sdrdrv_call(command, args);
}
