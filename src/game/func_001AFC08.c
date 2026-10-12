#include "common.h"

/* Writes the eased fraction of value between start and end to result (mode 1 squares it, mode 2 eases out)
   and its complement to complement. */
void func_001AFC08(f32 *complement, f32 *result, s32 value, s32 start, s32 end, s32 mode) {
    f32 fraction;
    f32 inverse;
    f32 start_f;

    start_f = (f32)start;
    fraction = ((f32)value - start_f) / ((f32)end - start_f);
    *result = fraction;
    if (mode == 1) {
        *result = fraction * fraction;
    } else if (mode == 2) {
        inverse = 1.0f - fraction;
        *result = 1.0f - (inverse * inverse);
    }
    *complement = 1.0f - *result;
}
