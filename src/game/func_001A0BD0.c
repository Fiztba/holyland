#include "common.h"

extern s32 D_00446864;
extern s32 (*D_0043ED88[])(void);

s32 func_001A0BD0(void) {
    if (D_0043ED88[D_00446864]() > 0) {
        D_00446864 = 0;
        return 0;
    }
    return 1;
}
