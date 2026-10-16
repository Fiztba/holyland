/* VU0 macro-mode helpers: vector and matrix arithmetic on 16-byte vectors (four floats), and the transform,
   projection and lighting steps of the model drawing, which keep their matrices in VU0 registers between calls
   (vf1-vf4 the projection matrix, vf5-vf7 the light directions, vf8-vf11 the light colours, vf12/vf13 the fog
   terms). */
#include "math/vu0.h"

/* Copies one 16-byte vector (the same code as libvu0's sceVu0CopyVector). */
void Vector_Copy(void *dst, void *src) {
    memcpy(dst, src, 16); /* lq/sq through $6 */
}

/* Copies one 16-byte vector: the same code as Vector_Copy, a second copy of it. */
void Vector_CopyQuadword(void *dst, void *src) {
    memcpy(dst, src, 16); /* lq/sq through $6 */
}

/* Loads the matrix the projections apply into vf1 to vf4. */
void Vu0_LoadMatrix(void *matrix) {
    Vu_Lqc2(1, (u8 *)matrix + 0x0);
    Vu_Lqc2(2, (u8 *)matrix + 0x10);
    Vu_Lqc2(3, (u8 *)matrix + 0x20);
    Vu_Lqc2(4, (u8 *)matrix + 0x30);
}

/* Loads the three light directions Vu0_LightColor takes the normal against into vf5 to vf7. */
void Vu0_LoadLightDirections(void *rows) {
    Vu_Lqc2(5, (u8 *)rows + 0x0);
    Vu_Lqc2(6, (u8 *)rows + 0x10);
    Vu_Lqc2(7, (u8 *)rows + 0x20);
}

/* Loads the three light colours and the ambient colour (the last row) Vu0_LightColor uses into vf8 to vf11. */
void Vu0_LoadLightColors(void *matrix) {
    Vu_Lqc2(8, (u8 *)matrix + 0x0);
    Vu_Lqc2(9, (u8 *)matrix + 0x10);
    Vu_Lqc2(10, (u8 *)matrix + 0x20);
    Vu_Lqc2(11, (u8 *)matrix + 0x30);
}

/* Loads the two terms the fog projections build w from (w = first.w + second.w / w) into vf12 and vf13. */
void Vu0_LoadFogTerms(void *first, void *second) {
    Vu_Lqc2(12, first);
    Vu_Lqc2(13, second);
}

/* Applies the vf1-vf4 matrix to a point, divides by w and stores the result as fixed point with 4 fraction
   bits: the screen position. */
void Vu0_ProjectToScreen(void *screen, void *point) {
    Vu_Lqc2(16, point);
    Vu_MulA(VU_XYZW, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 4, VF(16)[3]);
    Vu_Div(VF(0)[3], VF(16)[3]);
    Vu_MulBc(VU_XYZW, 26, 16, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 16, 26, 4);
    Vu_Sqc2(screen, 16);
}

/* Vu0_ProjectToScreen with the fog value in w: vf12.w + vf13.w / w. */
void Vu0_ProjectToScreenFog(void *screen, void *point) {
    Vu_Lqc2(16, point);
    Vu_MulA(VU_XYZW, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 4, VF(16)[3]);
    Vu_Div(VF(0)[3], VF(16)[3]);
    Vu_MulA(VU_W, 12, VF(0)[3]);
    Vu_Madd(VU_W, 26, 13, ee_vu0.q);
    Vu_MulBc(VU_XYZ, 26, 16, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 16, 26, 4);
    Vu_Sqc2(screen, 16);
}

/* Vu0_ProjectToScreen for the three points of a triangle, interleaved. */
void Vu0_ProjectToScreen3(void *screen0, void *screen1, void *screen2, void *point0, void *point1, void *point2) {
    /* The EE interleaves the three points; each reads only its own registers, so one after the other leaves
       VU0 (vf16-vf18, vf26-vf28, ACC, Q) as the EE did. */
    Vu_Lqc2(16, point0);
    Vu_MulA(VU_XYZW, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 4, VF(16)[3]);
    Vu_Div(VF(0)[3], VF(16)[3]);
    Vu_MulBc(VU_XYZW, 26, 16, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 16, 26, 4);
    Vu_Sqc2(screen0, 16);
    Vu_Lqc2(17, point1);
    Vu_MulA(VU_XYZW, 1, VF(17)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(17)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(17)[2]);
    Vu_Madd(VU_XYZW, 17, 4, VF(17)[3]);
    Vu_Div(VF(0)[3], VF(17)[3]);
    Vu_MulBc(VU_XYZW, 27, 17, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 17, 27, 4);
    Vu_Sqc2(screen1, 17);
    Vu_Lqc2(18, point2);
    Vu_MulA(VU_XYZW, 1, VF(18)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(18)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(18)[2]);
    Vu_Madd(VU_XYZW, 18, 4, VF(18)[3]);
    Vu_Div(VF(0)[3], VF(18)[3]);
    Vu_MulBc(VU_XYZW, 28, 18, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 18, 28, 4);
    Vu_Sqc2(screen2, 18);
}

/* Vu0_ProjectToScreenFog for the three points of a triangle, interleaved. */
void Vu0_ProjectToScreenFog3(void *screen0, void *screen1, void *screen2, void *point0, void *point1, void *point2) {
    /* The EE interleaves the three points; each reads only its own registers, so one after the other leaves
       VU0 (vf16-vf18, vf26-vf28, ACC, Q) as the EE did. */
    Vu_Lqc2(16, point0);
    Vu_MulA(VU_XYZW, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 4, VF(16)[3]);
    Vu_Div(VF(0)[3], VF(16)[3]);
    Vu_MulA(VU_W, 12, VF(0)[3]);
    Vu_Madd(VU_W, 26, 13, ee_vu0.q);
    Vu_MulBc(VU_XYZ, 26, 16, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 16, 26, 4);
    Vu_Sqc2(screen0, 16);
    Vu_Lqc2(17, point1);
    Vu_MulA(VU_XYZW, 1, VF(17)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(17)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(17)[2]);
    Vu_Madd(VU_XYZW, 17, 4, VF(17)[3]);
    Vu_Div(VF(0)[3], VF(17)[3]);
    Vu_MulA(VU_W, 12, VF(0)[3]);
    Vu_Madd(VU_W, 27, 13, ee_vu0.q);
    Vu_MulBc(VU_XYZ, 27, 17, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 17, 27, 4);
    Vu_Sqc2(screen1, 17);
    Vu_Lqc2(18, point2);
    Vu_MulA(VU_XYZW, 1, VF(18)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(18)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(18)[2]);
    Vu_Madd(VU_XYZW, 18, 4, VF(18)[3]);
    Vu_Div(VF(0)[3], VF(18)[3]);
    Vu_MulA(VU_W, 12, VF(0)[3]);
    Vu_Madd(VU_W, 28, 13, ee_vu0.q);
    Vu_MulBc(VU_XYZ, 28, 18, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 18, 28, 4);
    Vu_Sqc2(screen2, 18);
}

/* Lights a vertex: the normal against the vf5-vf7 directions (negative amounts count as 0), those amounts
   through the vf8-vf11 colours plus the ambient row, times the vertex colour; stored as integers. */
void Vu0_LightColor(void *color_out, void *normal, void *color) {
    Vu_Lqc2(16, normal);
    Vu_Lqc2(19, color);
    Vu_MulA(VU_XYZW, 5, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 6, VF(16)[1]);
    Vu_Madd(VU_XYZW, 16, 7, VF(16)[2]);
    Vu_MaxBc(VU_XYZW, 16, 16, VF(0)[0]);
    Vu_MulA(VU_XYZW, 8, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 9, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 10, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 11, VF(0)[3]);
    Vu_Mul(VU_XYZW, 16, 16, 19);
    Vu_Ftoi(VU_XYZW, 16, 16, 0);
    Vu_Sqc2(color_out, 16);
}

/* Vu0_LightColor for the three vertices of a triangle, interleaved. */
void Vu0_LightColor3(void *color_out0, void *color_out1, void *color_out2, void *normal0, void *normal1, void *normal2,
                     void *color0, void *color1, void *color2) {
    /* The EE interleaves the three vertices; each reads only its own registers (vf16/vf19, vf17/vf20,
       vf18/vf21), so one after the other leaves VU0 as the EE did. */
    Vu_Lqc2(16, normal0);
    Vu_Lqc2(19, color0);
    Vu_MulA(VU_XYZW, 5, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 6, VF(16)[1]);
    Vu_Madd(VU_XYZW, 16, 7, VF(16)[2]);
    Vu_MaxBc(VU_XYZW, 16, 16, VF(0)[0]);
    Vu_MulA(VU_XYZW, 8, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 9, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 10, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 11, VF(0)[3]);
    Vu_Mul(VU_XYZW, 16, 16, 19);
    Vu_Ftoi(VU_XYZW, 16, 16, 0);
    Vu_Sqc2(color_out0, 16);
    Vu_Lqc2(17, normal1);
    Vu_Lqc2(20, color1);
    Vu_MulA(VU_XYZW, 5, VF(17)[0]);
    Vu_MaddA(VU_XYZW, 6, VF(17)[1]);
    Vu_Madd(VU_XYZW, 17, 7, VF(17)[2]);
    Vu_MaxBc(VU_XYZW, 17, 17, VF(0)[0]);
    Vu_MulA(VU_XYZW, 8, VF(17)[0]);
    Vu_MaddA(VU_XYZW, 9, VF(17)[1]);
    Vu_MaddA(VU_XYZW, 10, VF(17)[2]);
    Vu_Madd(VU_XYZW, 17, 11, VF(0)[3]);
    Vu_Mul(VU_XYZW, 17, 17, 20);
    Vu_Ftoi(VU_XYZW, 17, 17, 0);
    Vu_Sqc2(color_out1, 17);
    Vu_Lqc2(18, normal2);
    Vu_Lqc2(21, color2);
    Vu_MulA(VU_XYZW, 5, VF(18)[0]);
    Vu_MaddA(VU_XYZW, 6, VF(18)[1]);
    Vu_Madd(VU_XYZW, 18, 7, VF(18)[2]);
    Vu_MaxBc(VU_XYZW, 18, 18, VF(0)[0]);
    Vu_MulA(VU_XYZW, 8, VF(18)[0]);
    Vu_MaddA(VU_XYZW, 9, VF(18)[1]);
    Vu_MaddA(VU_XYZW, 10, VF(18)[2]);
    Vu_Madd(VU_XYZW, 18, 11, VF(0)[3]);
    Vu_Mul(VU_XYZW, 18, 18, 21);
    Vu_Ftoi(VU_XYZW, 18, 18, 0);
    Vu_Sqc2(color_out2, 18);
}

/* dst = matrix * src, 4x4 matrices. */
void Matrix_Multiply(void *dst, void *matrix, void *src) {
    Vu_Lqc2(1, (u8 *)matrix + 0x0);
    Vu_Lqc2(2, (u8 *)matrix + 0x10);
    Vu_Lqc2(3, (u8 *)matrix + 0x20);
    Vu_Lqc2(4, (u8 *)matrix + 0x30);
    Vu_Lqc2(22, (u8 *)src + 0x0);
    Vu_Lqc2(23, (u8 *)src + 0x10);
    Vu_Lqc2(24, (u8 *)src + 0x20);
    Vu_Lqc2(25, (u8 *)src + 0x30);
    Vu_MulA(VU_XYZW, 1, VF(22)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(22)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(22)[2]);
    Vu_Madd(VU_XYZW, 22, 4, VF(22)[3]);
    Vu_MulA(VU_XYZW, 1, VF(23)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(23)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(23)[2]);
    Vu_Madd(VU_XYZW, 23, 4, VF(23)[3]);
    Vu_MulA(VU_XYZW, 1, VF(24)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(24)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(24)[2]);
    Vu_Madd(VU_XYZW, 24, 4, VF(24)[3]);
    Vu_MulA(VU_XYZW, 1, VF(25)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(25)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(25)[2]);
    Vu_Madd(VU_XYZW, 25, 4, VF(25)[3]);
    Vu_Sqc2((u8 *)dst + 0x0, 22);
    Vu_Sqc2((u8 *)dst + 0x10, 23);
    Vu_Sqc2((u8 *)dst + 0x20, 24);
    Vu_Sqc2((u8 *)dst + 0x30, 25);
}

/* dst = matrix * src, all four components. */
void Vector_ApplyMatrix(void *dst, void *matrix, void *src) {
    Vu_Lqc2(1, (u8 *)matrix + 0x0);
    Vu_Lqc2(2, (u8 *)matrix + 0x10);
    Vu_Lqc2(3, (u8 *)matrix + 0x20);
    Vu_Lqc2(4, (u8 *)matrix + 0x30);
    Vu_Lqc2(16, src);
    Vu_MulA(VU_XYZW, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(16)[2]);
    Vu_Madd(VU_XYZW, 16, 4, VF(16)[3]);
    Vu_Sqc2(dst, 16);
}

/* dst = lhs * rhs component by component, all four components. */
void Vector_MultiplyComponents(void *dst, void *lhs, void *rhs) {
    Vu_Lqc2(16, lhs);
    Vu_Lqc2(17, rhs);
    Vu_Mul(VU_XYZW, 16, 16, 17);
    Vu_Sqc2(dst, 16);
}

/* Converts a float vector to fixed point with 4 fraction bits. */
void Vector_ToFixed4(void *dst, void *src) {
    Vu_Lqc2(16, src);
    Vu_Ftoi(VU_XYZW, 16, 16, 4);
    Vu_Sqc2(dst, 16);
}

/* Converts a float vector to integers. */
void Vector_ToInt(void *dst, void *src) {
    Vu_Lqc2(16, src);
    Vu_Ftoi(VU_XYZW, 16, 16, 0);
    Vu_Sqc2(dst, 16);
}

/* Converts a fixed point vector with 4 fraction bits to floats. */
void Vector_FromFixed4(void *dst, void *src) {
    Vu_Lqc2(16, src);
    Vu_Itof(VU_XYZW, 16, 16, 4);
    Vu_Sqc2(dst, 16);
}

/* Converts an integer vector to floats. */
void Vector_FromInt(void *dst, void *src) {
    Vu_Lqc2(16, src);
    Vu_Itof(VU_XYZW, 16, 16, 0);
    Vu_Sqc2(dst, 16);
}

/* dst = lhs + rhs (x, y, z only). */
void Vector_Add(void *dst, void *lhs, void *rhs) {
    Vu_Lqc2(16, lhs);
    Vu_Lqc2(17, rhs);
    Vu_Add(VU_XYZ, 16, 16, 17);
    Vu_Sqc2(dst, 16);
}

/* dst = lhs + rhs, all four components. */
void Vector_Add4(void *dst, void *lhs, void *rhs) {
    Vu_Lqc2(16, lhs);
    Vu_Lqc2(17, rhs);
    Vu_Add(VU_XYZW, 16, 16, 17);
    Vu_Sqc2(dst, 16);
}

/* dst = lhs - rhs (x, y, z only). */
void Vector_Subtract(void *dst, void *lhs, void *rhs) {
    Vu_Lqc2(16, lhs);
    Vu_Lqc2(17, rhs);
    Vu_Sub(VU_XYZ, 16, 16, 17);
    Vu_Sqc2(dst, 16);
}

/* dst = lhs - rhs, all four components. */
void Vector_Subtract4(void *dst, void *lhs, void *rhs) {
    Vu_Lqc2(16, lhs);
    Vu_Lqc2(17, rhs);
    Vu_Sub(VU_XYZW, 16, 16, 17);
    Vu_Sqc2(dst, 16);
}

/* dst = src * scale (x, y, z only). */
void Vector_Scale(void *dst, void *src, float scale) {
    /* mfc1/qmtc2: vf31.x = scale, vf31.y its sign extension (z and w are whatever the upper half of $2 held) */
    ee_vu0.vf[31][0] = scale;
    ee_vu0.vf[31][1] = 0.0f;
    if (signbit(scale)) {
        u32 ones = 0xFFFFFFFF;
        memcpy(&ee_vu0.vf[31][1], &ones, 4);
    }
    Vu_Lqc2(16, src);
    Vu_MulBc(VU_XYZ, 16, 16, VF(31)[0]);
    Vu_Sqc2(dst, 16);
}

/* dst = src * scale, all four components. */
void Vector_Scale4(void *dst, void *src, float scale) {
    /* mfc1/qmtc2: vf31.x = scale, vf31.y its sign extension (z and w are whatever the upper half of $2 held) */
    ee_vu0.vf[31][0] = scale;
    ee_vu0.vf[31][1] = 0.0f;
    if (signbit(scale)) {
        u32 ones = 0xFFFFFFFF;
        memcpy(&ee_vu0.vf[31][1], &ones, 4);
    }
    Vu_Lqc2(16, src);
    Vu_MulBc(VU_XYZW, 16, 16, VF(31)[0]);
    Vu_Sqc2(dst, 16);
}

/* dst = lhs x rhs, the cross product. */
void Vector_Cross(void *dst, void *lhs, void *rhs) {
    float a[4], b[4];

    Vu_Lqc2(16, lhs);
    Vu_Lqc2(17, rhs);
    memcpy(a, VF(16), 16);
    memcpy(b, VF(17), 16);
    /* vopmula.xyz ACC, vf16, vf17 */
    ee_vu0.acc[0] = a[1] * b[2];
    ee_vu0.acc[1] = a[2] * b[0];
    ee_vu0.acc[2] = a[0] * b[1];
    /* vopmsub.xyz vf16, vf17, vf16 */
    ee_vu0.vf[16][0] = ee_vu0.acc[0] - b[1] * a[2];
    ee_vu0.vf[16][1] = ee_vu0.acc[1] - b[2] * a[0];
    ee_vu0.vf[16][2] = ee_vu0.acc[2] - b[0] * a[1];
    Vu_Sqc2(dst, 16);
}

/* dst = vector0 * weights.x + vector1 * weights.y + vector2 * weights.z + vector3 * weights.w (x, y, z only);
   the callers blend two points and two tangents along a curve with it. */
void Vector_WeightedSum4(void *dst, void *vector0, void *vector1, void *vector2, void *vector3, float *weights) {
    Vu_Lqc2(16, weights);
    Vu_Lqc2(1, vector0);
    Vu_Lqc2(2, vector1);
    Vu_Lqc2(3, vector2);
    Vu_Lqc2(4, vector3);
    Vu_MulA(VU_XYZ, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZ, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZ, 3, VF(16)[2]);
    Vu_Madd(VU_XYZ, 16, 4, VF(16)[3]);
    Vu_Sqc2(dst, 16);
}

/* dst = src scaled to length 1 (x, y, z only). */
void Vector_Normalize(void *dst, void *src) {
    Vu_Lqc2(16, src);
    Vu_Mul(VU_XYZ, 17, 16, 16);
    Vu_AddBc(VU_Z, 17, 17, VF(17)[0]);
    Vu_AddBc(VU_Z, 17, 17, VF(17)[1]);
    Vu_Sqrt(VF(17)[2]);
    Vu_MulBc(VU_XYZW, 17, 0, ee_vu0.q);
    Vu_Div(VF(0)[3], VF(17)[3]);
    Vu_MulBc(VU_XYZ, 16, 16, ee_vu0.q);
    Vu_Sqc2(dst, 16);
}
