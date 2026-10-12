#include "common.h"

void KF_Vector_Subtract(float *, float *, float *);
void func_001AF5B0(void *, void *);

void func_001A7110(void *angles, float *target, float *origin) {
    float delta[4];

    KF_Vector_Subtract(delta, target, origin);
    func_001AF5B0(angles, delta);
}
