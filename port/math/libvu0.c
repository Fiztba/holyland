/* libvu0's vector and matrix calls in C (port/include/libvu0.h). A matrix is four rows m[0..3] that
   act as the columns of the transform: ApplyMatrix gives m[0]*x + m[1]*y + m[2]*z + m[3]*w.
   The EE does these sums on VU0 in single precision; the order of the sums here follows the VU0 code
   (multiply by x, then add the y, z and w terms). Rounding still differs from the EE's (no IEEE
   denormals or round-to-nearest on the EE); port/math will model that later where it matters. */
#include <math.h>
#include <string.h>

#include "libvu0.h"

typedef float Vec[4];
typedef float Mat[4][4];

void sceVu0AddVector(void *dst, void *a, void *b) {
    float *d = dst, *x = a, *y = b;
    int i;

    for (i = 0; i < 4; i++) {
        d[i] = x[i] + y[i];
    }
}

void sceVu0SubVector(void *dst, void *a, void *b) {
    float *d = dst, *x = a, *y = b;
    int i;

    for (i = 0; i < 4; i++) {
        d[i] = x[i] - y[i];
    }
}

void sceVu0MulVector(void *dst, void *a, void *b) {
    float *d = dst, *x = a, *y = b;
    int i;

    for (i = 0; i < 4; i++) {
        d[i] = x[i] * y[i];
    }
}

void sceVu0ScaleVector(void *dst, void *src, float scale) {
    float *d = dst, *s = src;
    int i;

    for (i = 0; i < 4; i++) {
        d[i] = s[i] * scale;
    }
}

void sceVu0ScaleVectorXYZ(void *dst, void *src, float scale) {
    float *d = dst, *s = src;

    d[0] = s[0] * scale;
    d[1] = s[1] * scale;
    d[2] = s[2] * scale;
    d[3] = s[3];
}

/* VU0 divides once (Q = 1 / q) and multiplies */
void sceVu0DivVector(void *dst, void *src, float q) {
    float *d = dst, *s = src;
    float inverse = 1.0f / q;
    int i;

    for (i = 0; i < 4; i++) {
        d[i] = s[i] * inverse;
    }
}

void sceVu0DivVectorXYZ(void *dst, void *src, float q) {
    float *d = dst, *s = src;
    float inverse = 1.0f / q;

    d[0] = s[0] * inverse;
    d[1] = s[1] * inverse;
    d[2] = s[2] * inverse;
    d[3] = s[3];
}

void sceVu0CopyVector(void *dst, void *src) {
    memmove(dst, src, sizeof(Vec));
}

float sceVu0InnerProduct(void *a, void *b) {
    float *x = a, *y = b;

    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

void sceVu0OuterProduct(void *dst, void *a, void *b) {
    float *x = a, *y = b;
    Vec r;

    r[0] = x[1] * y[2] - x[2] * y[1];
    r[1] = x[2] * y[0] - x[0] * y[2];
    r[2] = x[0] * y[1] - x[1] * y[0];
    r[3] = 0.0f;
    memcpy(dst, r, sizeof(r));
}

/* xyz scaled to length 1 (VU0's vsqrt takes the absolute value); w becomes 0 */
void sceVu0Normalize(void *dst, void *src) {
    float *d = dst, *s = src;
    float length = sqrtf(fabsf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]));
    float scale = 1.0f / length;

    d[0] = s[0] * scale;
    d[1] = s[1] * scale;
    d[2] = s[2] * scale;
    d[3] = 0.0f;
}

void sceVu0ApplyMatrix(void *dst, void *m, void *v) {
    float (*a)[4] = m;
    float *x = v;
    Vec r;
    int i;

    for (i = 0; i < 4; i++) {
        r[i] = a[0][i] * x[0] + a[1][i] * x[1] + a[2][i] * x[2] + a[3][i] * x[3];
    }
    memcpy(dst, r, sizeof(r));
}

void sceVu0MulMatrix(void *dst, void *m0, void *m1) {
    float (*b)[4] = m1;
    Mat r;
    int i;

    for (i = 0; i < 4; i++) {
        sceVu0ApplyMatrix(r[i], m0, b[i]);
    }
    memcpy(dst, r, sizeof(r));
}

void sceVu0UnitMatrix(void *m) {
    static const Mat unit = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    memcpy(m, unit, sizeof(unit));
}

void sceVu0CopyMatrix(void *dst, void *src) {
    memmove(dst, src, sizeof(Mat));
}

void sceVu0TransposeMatrix(void *dst, void *src) {
    float (*s)[4] = src;
    Mat r;
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            r[i][j] = s[j][i];
        }
    }
    memcpy(dst, r, sizeof(r));
}

/* The inverse of a rotation and translation: the rotation transposed, the translation turned back
   through it. */
void sceVu0InversMatrix(void *dst, void *src) {
    float (*s)[4] = src;
    Mat r;
    int i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            r[i][j] = s[j][i];
        }
        r[i][3] = 0.0f;
    }
    for (i = 0; i < 3; i++) {
        r[3][i] = -(r[0][i] * s[3][0] + r[1][i] * s[3][1] + r[2][i] * s[3][2]);
    }
    r[3][3] = s[3][3];
    memcpy(dst, r, sizeof(r));
}

/* libvu0's _sceVu0ecossin: cos(angle) as sin(pi/2 - |angle|) by an odd polynomial to t^9 (summed in
   VU0's order), and sin as the square root of 1 - cos^2 with the sign of the angle. Exact within
   [-pi, pi]; outside it the polynomial drifts as retail's does, and the game relies on retail. */
static void ecossin(float angle, float *cos_out, float *sin_out) {
    static const float coeff[4] = {2.601887e-06f, -0.00019807414f, 0.0083330255f, -0.16666657f};
    const float half_pi = 1.57079637f; /* 0x3FC90FDB */
    int negative = angle < 0.0f;
    float t = negative ? half_pi + angle : half_pi - angle;
    float t2 = t * t;
    float term[4];
    float sum, root;
    int i;

    for (i = 0; i < 4; i++) {
        term[i] = coeff[i] * t * t2;
    }
    term[0] *= t2;
    term[1] *= t2;
    term[2] *= t2;
    sum = t + term[3];
    term[0] *= t2;
    term[1] *= t2;
    sum = sum + term[2];
    term[0] *= t2;
    sum = sum + term[1];
    sum = sum + term[0];
    root = sqrtf(fabsf(1.0f - sum * sum));
    *cos_out = sum;
    *sin_out = negative ? 0.0f - root : 0.0f + root;
}

static void rotate(void *dst, void *src, int axis, float angle) {
    Mat rot;
    float c, s;
    int a = (axis + 1) % 3, b = (axis + 2) % 3;

    ecossin(angle, &c, &s);

    sceVu0UnitMatrix(rot);
    rot[a][a] = c;
    rot[a][b] = s;
    rot[b][a] = -s;
    rot[b][b] = c;
    sceVu0MulMatrix(dst, rot, src);
}

void sceVu0RotMatrixX(void *dst, void *src, float angle) {
    rotate(dst, src, 0, angle);
}

void sceVu0RotMatrixY(void *dst, void *src, float angle) {
    rotate(dst, src, 1, angle);
}

void sceVu0RotMatrixZ(void *dst, void *src, float angle) {
    rotate(dst, src, 2, angle);
}

void sceVu0TransMatrix(void *dst, void *src, void *translation) {
    float (*d)[4] = dst;
    float *t = translation;

    sceVu0CopyMatrix(dst, src);
    d[3][0] += t[0];
    d[3][1] += t[1];
    d[3][2] += t[2];
}

void sceVu0ViewScreenMatrix(void *dst, float scrz, float ax, float ay, float cx, float cy, float zmin,
                            float zmax, float nearz, float farz) {
    float (*m)[4] = dst;
    float az = farz * nearz * (zmax - zmin) / (farz - nearz);
    float cz = (farz * zmin - zmax * nearz) / (farz - nearz);

    memset(m, 0, sizeof(Mat));
    m[0][0] = scrz * ax;
    m[1][1] = scrz * ay;
    m[2][0] = cx;
    m[2][1] = cy;
    m[2][2] = cz;
    m[2][3] = 1.0f;
    m[3][2] = az;
}
