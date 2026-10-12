#include "common.h"
#include "kf_game.h"
#include "kf_mc.h"
#include "kf_state.h"

extern ModeState D_005CF3F0[];
extern ModeState D_005CF410[];
extern char *D_00447334;
extern ScratchSlot *D_0044733C;

int func_001F97E0(void);
int func_001FAEC0(void);

/* Updates the pending mode request; falls back to the current mode's handler. */
int func_001F9648(void) {
    ModeState *request;
    ModeState *current;
    int result;

    request = D_005CF3F0;
    if (request->state == 1 && SLOT_RECORD_BYTES(&D_00428B80[0])->first != 2 &&
        SLOT_RECORD_BYTES(&D_00428B80[1])->first != 2) {
        result = func_001F97E0();
        current = D_005CF410;
        current->unk1C = 1;
        if (current->state == 1) {
            if (result != 1) {
                return 0;
            }
            current->state = 0;
            current->step = 0;
            current->retries = 0;
            current->unk1C = 0;
            return 1;
        }
        if (current->state == 0) {
            if (result == 1) {
                if (request->unk18 == 0) {
                    request->unk1B = 0;
                    request->state = 0;
                    D_00447338 = NULL;
                    D_00447334 = NULL;
                    D_0044733C = NULL;
                    return -1;
                }
                request->unk1B = result;
                current->unk1C = 0;
                request->step = 0;
            }
            return 1;
        }
        if (result == 0) {
            return 0;
        }
    }
    return func_001FAEC0();
}
