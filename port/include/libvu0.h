/*
 * port/include/libvu0.h
 *
 * libvu0 for the PC port: the vector and matrix calls, in C (port/math/libvu0.c), each doing its sums
 * in the order the EE's VU0 code does. Vectors are four floats, matrices four vectors (rows), both
 * 16-byte aligned on the EE; the C versions do not need the alignment.
 */
#ifndef SCE_LIBVU0_LIBVU0_H
#define SCE_LIBVU0_LIBVU0_H

typedef float sceVu0FVECTOR[4] __attribute__((aligned(16)));
typedef float sceVu0FMATRIX[4][4] __attribute__((aligned(16)));
typedef int sceVu0IVECTOR[4] __attribute__((aligned(16)));

void sceVu0AddVector(void *dst, void *a, void *b);
void sceVu0SubVector(void *dst, void *a, void *b);
void sceVu0MulVector(void *dst, void *a, void *b);
void sceVu0ScaleVector(void *dst, void *src, float scale);
void sceVu0ScaleVectorXYZ(void *dst, void *src, float scale);
void sceVu0DivVector(void *dst, void *src, float q);
void sceVu0DivVectorXYZ(void *dst, void *src, float q);
void sceVu0CopyVector(void *dst, void *src);
float sceVu0InnerProduct(void *a, void *b);
void sceVu0OuterProduct(void *dst, void *a, void *b);
void sceVu0Normalize(void *dst, void *src);
void sceVu0ApplyMatrix(void *dst, void *m, void *v);
void sceVu0MulMatrix(void *dst, void *m0, void *m1);
void sceVu0UnitMatrix(void *m);
void sceVu0CopyMatrix(void *dst, void *src);
void sceVu0TransposeMatrix(void *dst, void *src);
void sceVu0InversMatrix(void *dst, void *src);
void sceVu0RotMatrixX(void *dst, void *src, float angle);
void sceVu0RotMatrixY(void *dst, void *src, float angle);
void sceVu0RotMatrixZ(void *dst, void *src, float angle);
void sceVu0TransMatrix(void *dst, void *src, void *translation);
void sceVu0ViewScreenMatrix(void *dst, float scrz, float ax, float ay, float cx, float cy, float zmin,
                            float zmax, float nearz, float farz);

#endif /* SCE_LIBVU0_LIBVU0_H */
