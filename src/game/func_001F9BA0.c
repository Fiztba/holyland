#include "common.h"
#include "kf_state.h"

extern ModeState D_005CF410[];
extern char D_00447340;

void func_001FBAC8(void);

/* Starts mode 3 for the given slot unless another mode is running. */
s32 func_001F9BA0(s32 slot, char sub_mode) {
    if (D_005CF410->state != 0) {
        return -1;
    }
    D_00428B80[0].unk06 = 0;
    D_00428B80[1].unk06 = 0;
    func_001FBAC8();
    D_005CF410->slot = slot;
    D_005CF410->state = D_005CF410->unk12 = 3;
    D_005CF410->phase = 0;
    D_005CF410->step = 0;
    D_005CF410->count = 0;
    D_005CF410->retries = 0;
    D_00447340 = sub_mode;
    return 0;
}
