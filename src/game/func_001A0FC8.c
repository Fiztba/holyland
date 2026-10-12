#include "common.h"

extern s32 D_00446868;
extern s32 D_004468B4;
extern s8 D_004470E0;
extern s8 D_004470E1;

s32 func_001A0FC8(void) {
    D_004468B4 = 0;
    D_00446868 += 1;
    if (D_004470E0 != 0) {
        D_004470E1 = TRUE;
        return 1;
    }
    D_004470E0 = TRUE;
    return 0;
}
