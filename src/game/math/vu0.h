/* VU0 vector and matrix helpers (math/vu0.c). Vectors are 16 bytes, four floats (or four ints after a
   conversion); matrices are four such rows. */
#ifndef KF_VU0_H
#define KF_VU0_H

#include "common.h"

/* Copies one 16-byte vector. */
void Vector_Copy(void *dst, void *src);

/* Copies one 16-byte vector: the same code as Vector_Copy. */
void Vector_CopyQuadword(void *dst, void *src);

/* Loads the matrix the projections apply into VU0 (vf1-vf4). */
void Vu0_LoadMatrix(void *matrix);

/* Loads the three light directions Vu0_LightColor uses (vf5-vf7). */
void Vu0_LoadLightDirections(void *rows);

/* Loads the three light colours and the ambient colour Vu0_LightColor uses (vf8-vf11). */
void Vu0_LoadLightColors(void *matrix);

/* Loads the two terms the fog projections build w from (vf12, vf13). */
void Vu0_LoadFogTerms(void *first, void *second);

/* Projects a point with the loaded matrix and stores the screen position as fixed point with 4 fraction bits. */
void Vu0_ProjectToScreen(void *screen, void *point);

/* Vu0_ProjectToScreen with the fog value in w. */
void Vu0_ProjectToScreenFog(void *screen, void *point);

/* Vu0_ProjectToScreen for three points. */
void Vu0_ProjectToScreen3(void *screen0, void *screen1, void *screen2, void *point0, void *point1, void *point2);

/* Vu0_ProjectToScreenFog for three points. */
void Vu0_ProjectToScreenFog3(void *screen0, void *screen1, void *screen2, void *point0, void *point1, void *point2);

/* Lights a vertex colour with the loaded lights and stores it as integers. */
void Vu0_LightColor(void *color_out, void *normal, void *color);

/* Vu0_LightColor for three vertices. */
void Vu0_LightColor3(void *color_out0, void *color_out1, void *color_out2, void *normal0, void *normal1, void *normal2,
                     void *color0, void *color1, void *color2);

/* dst = matrix * src, 4x4 matrices. */
void Matrix_Multiply(void *dst, void *matrix, void *src);

/* dst = matrix * src, all four components. */
void Vector_ApplyMatrix(void *dst, void *matrix, void *src);

/* dst = lhs * rhs component by component. */
void Vector_MultiplyComponents(void *dst, void *lhs, void *rhs);

/* Float vector to fixed point with 4 fraction bits. */
void Vector_ToFixed4(void *dst, void *src);

/* Float vector to integers. */
void Vector_ToInt(void *dst, void *src);

/* Fixed point vector with 4 fraction bits to floats. */
void Vector_FromFixed4(void *dst, void *src);

/* Integer vector to floats. */
void Vector_FromInt(void *dst, void *src);

/* dst = lhs + rhs (x, y, z). */
void Vector_Add(void *dst, void *lhs, void *rhs);

/* dst = lhs + rhs (all four). */
void Vector_Add4(void *dst, void *lhs, void *rhs);

/* dst = lhs - rhs (x, y, z). */
void Vector_Subtract(void *dst, void *lhs, void *rhs);

/* dst = lhs - rhs (all four). */
void Vector_Subtract4(void *dst, void *lhs, void *rhs);

/* dst = src * scale (x, y, z). */
void Vector_Scale(void *dst, void *src, float scale);

/* dst = src * scale (all four). */
void Vector_Scale4(void *dst, void *src, float scale);

/* dst = lhs x rhs. */
void Vector_Cross(void *dst, void *lhs, void *rhs);

/* dst = the four vectors weighted by the four components of weights (x, y, z). */
void Vector_WeightedSum4(void *dst, void *vector0, void *vector1, void *vector2, void *vector3, float *weights);

/* dst = src scaled to length 1 (x, y, z). */
void Vector_Normalize(void *dst, void *src);

/* ---- VU0 macro-mode instructions in C (the PC port) ------------------------------------------
   The converted inline asm of math/vu0.c and math/vu0_macro.c runs on ee_vu0 (port/host.h), so the
   matrices and vectors the game leaves in VU0 registers between calls stay there. Each helper is one
   instruction; mask is its field mask (VU_X | VU_Y | VU_Z | VU_W), a broadcast operand is passed as
   the float it reads. vf0 is never written. Integer results (vftoi) keep their bits in the float
   slots, as on VU0. */
#include <math.h>
#include <string.h>

#define VU_X 1
#define VU_Y 2
#define VU_Z 4
#define VU_W 8
#define VU_XYZ (VU_X | VU_Y | VU_Z)
#define VU_XYZW (VU_X | VU_Y | VU_Z | VU_W)
#define VF(n) (ee_vu0.vf[n])

/* lqc2 / sqc2: a quadword between memory and vf[r] */
static inline void Vu_Lqc2(int r, const void *src) { memcpy(ee_vu0.vf[r], src, 16); }
static inline void Vu_Sqc2(void *dst, int r) { memcpy(dst, ee_vu0.vf[r], 16); }

/* vmula(bc): ACC = fs * t */
static inline void Vu_MulA(int mask, int fs, float t) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.acc[i] = ee_vu0.vf[fs][i] * t;
}
/* vmadda(bc): ACC = ACC + fs * t */
static inline void Vu_MaddA(int mask, int fs, float t) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.acc[i] = ee_vu0.acc[i] + ee_vu0.vf[fs][i] * t;
}
/* vmadd(bc): fd = ACC + fs * t */
static inline void Vu_Madd(int mask, int fd, int fs, float t) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.acc[i] + ee_vu0.vf[fs][i] * t;
}
/* vmul(bc/q): fd = fs * t */
static inline void Vu_MulBc(int mask, int fd, int fs, float t) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.vf[fs][i] * t;
}
/* vmul: fd = fs * ft, by component */
static inline void Vu_Mul(int mask, int fd, int fs, int ft) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.vf[fs][i] * ee_vu0.vf[ft][i];
}
/* vadd: fd = fs + ft, by component */
static inline void Vu_Add(int mask, int fd, int fs, int ft) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.vf[fs][i] + ee_vu0.vf[ft][i];
}
/* vadd(bc): fd = fs + t */
static inline void Vu_AddBc(int mask, int fd, int fs, float t) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.vf[fs][i] + t;
}
/* vsub: fd = fs - ft, by component */
static inline void Vu_Sub(int mask, int fd, int fs, int ft) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.vf[fs][i] - ee_vu0.vf[ft][i];
}
/* vmax(bc): fd = max(fs, t) */
static inline void Vu_MaxBc(int mask, int fd, int fs, float t) {
    int i;
    for (i = 0; i < 4; i++) if (mask & (1 << i)) ee_vu0.vf[fd][i] = ee_vu0.vf[fs][i] > t ? ee_vu0.vf[fs][i] : t;
}
/* One float to the integer vftoi gives: times 2^fraction_bits, truncated, saturated. */
static inline int32_t Vu_FloatToInt(float f, int fraction_bits) {
    float scaled = f * (float)(1 << fraction_bits);
    if (scaled >= 2147483648.0f) return INT32_MAX;
    if (scaled <= -2147483648.0f) return INT32_MIN;
    if (scaled != scaled) return 0; /* NaN: VU0 has none */
    return (int32_t)scaled;
}
/* vftoi0 / vftoi4: fd = fs as integers with 0 or 4 fraction bits */
static inline void Vu_Ftoi(int mask, int fd, int fs, int fraction_bits) {
    int i;
    for (i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            int32_t v = Vu_FloatToInt(ee_vu0.vf[fs][i], fraction_bits);
            memcpy(&ee_vu0.vf[fd][i], &v, 4);
        }
    }
}
/* vitof0 / vitof4: fd = the integers in fs, with 0 or 4 fraction bits, as floats */
static inline void Vu_Itof(int mask, int fd, int fs, int fraction_bits) {
    int i;
    for (i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            int32_t v;
            memcpy(&v, &ee_vu0.vf[fs][i], 4);
            ee_vu0.vf[fd][i] = (float)v / (float)(1 << fraction_bits);
        }
    }
}

/* vdiv: Q = num / den; dividing by zero gives the largest float with the quotient's sign, as on VU0 */
static inline void Vu_Div(float num, float den) {
    if (den == 0.0f) {
        ee_vu0.q = ((signbit(num) != 0) != (signbit(den) != 0)) ? -3.40282347e38f : 3.40282347e38f;
    } else {
        ee_vu0.q = num / den;
    }
}
/* vsqrt: Q = sqrt(|t|) */
static inline void Vu_Sqrt(float t) { ee_vu0.q = sqrtf(fabsf(t)); }

#endif
