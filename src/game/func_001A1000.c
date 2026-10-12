#include "common.h"

extern s32 D_00446868;
extern int (*const D_0043ED90[])(void);

/* Runs the current step's handler; a positive result restarts the step counter. */
s32 func_001A1000(void) {
    if (D_0043ED90[D_00446868]() > 0) {
        D_00446868 = 0;
        return 0;
    }
    return 1;
}
