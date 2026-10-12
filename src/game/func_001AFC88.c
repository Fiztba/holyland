#include "common.h"

void func_00216D20();
void KF_Vector_Scale(float, void *, void *);
void func_00216C48(void *, void *, void *);

void func_001AFC88(float speed, float weight, void *vec, void *unused) {
    int offset[4];
    func_00216D20(offset);
    KF_Vector_Scale(weight, offset, offset);
    func_00216C48(vec, vec, offset);
    func_00216D20(vec, vec);
    KF_Vector_Scale(speed, vec, vec);
}
