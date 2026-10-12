#include "kf_effect.h"
#include "kf_player.h"

extern PlayerState D_00413F80;
extern int D_00402D54[];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_0019CE00(int);

/* Runs the effect setup, then calls func_0019CE00 for the definition's kind (3: only if the player/param pair is in the table). */
void func_001AEB30(EffectActor *actor, int frame) {
    EffectDef *def;
    int i;
    int *key;
    int *param;

    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    def = actor->def;
    if (def->radius_half == 2) {
        func_0019CE00(def->radius_half);
    } else if (def->radius_half == 3) {
        i = 0;
        param = D_00402D54;
        key = param - 1;
        for (; i < 7; key += 2, i++) {
            if (D_00413F80.unk3C == *key && actor->effect_param == *param) {
                func_0019CE00(def->radius_half);
                break;
            }
            param += 2;
        }
    }
}
