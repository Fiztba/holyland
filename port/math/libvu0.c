/* libvu0's vector and matrix calls in C (port/include/libvu0.h). A matrix is four rows m[0..3] that
   act as the columns of the transform: ApplyMatrix gives m[0]*x + m[1]*y + m[2]*z + m[3]*w.

   Each call computes what the SDK's (decomp/src/sce/libvu0/libvu0.c) does: its VU0 instructions as VU
   float arithmetic (port/vu_float.h: rounded toward zero as VU1 rounds, no denormals, the square root
   of |x|, division by zero the largest float), one operation per lane in the order and with the fields
   of the asm; its EE C and COP1 code (ViewScreenMatrix's terms, the angle the rotations start from) as
   host floats. The SDK's calls also leave values in VU0 registers (vf4-vf10, ACC, Q); these don't, so
   a game that read those after a call would differ. */
#include <string.h>

#include "libvu0.h"
#include "port/vu_float.h"

typedef float Vec[4];
typedef float Mat[4][4];

void sceVu0AddVector(void *dst, void *a, void *b) {
    float *d = dst, *x = a, *y = b;
    int i;

    for (i = 0; i < 4; i++) { /* vadd.xyzw */
        d[i] = Vu_FAdd(x[i], y[i]);
    }
}

void sceVu0SubVector(void *dst, void *a, void *b) {
    float *d = dst, *x = a, *y = b;
    int i;

    for (i = 0; i < 4; i++) { /* vsub.xyzw */
        d[i] = Vu_FSub(x[i], y[i]);
    }
}

void sceVu0MulVector(void *dst, void *a, void *b) {
    float *d = dst, *x = a, *y = b;
    int i;

    for (i = 0; i < 4; i++) { /* vmul.xyzw */
        d[i] = Vu_FMul(x[i], y[i]);
    }
}

void sceVu0ScaleVector(void *dst, void *src, float scale) {
    float *d = dst, *s = src;
    int i;

    for (i = 0; i < 4; i++) { /* vmulx.xyzw */
        d[i] = Vu_FMul(s[i], scale);
    }
}

void sceVu0ScaleVectorXYZ(void *dst, void *src, float scale) {
    float *d = dst, *s = src;
    float w = s[3];

    d[0] = Vu_FMul(s[0], scale); /* vmulx.xyz */
    d[1] = Vu_FMul(s[1], scale);
    d[2] = Vu_FMul(s[2], scale);
    d[3] = w;
}

/* vdiv Q, vf0w, q, then vmulq: one division, then the products */
void sceVu0DivVector(void *dst, void *src, float q) {
    float *d = dst, *s = src;
    float inverse = Vu_FDiv(1.0f, q);
    int i;

    for (i = 0; i < 4; i++) {
        d[i] = Vu_FMul(s[i], inverse);
    }
}

void sceVu0DivVectorXYZ(void *dst, void *src, float q) {
    float *d = dst, *s = src;
    float inverse = Vu_FDiv(1.0f, q);
    float w = s[3];

    d[0] = Vu_FMul(s[0], inverse);
    d[1] = Vu_FMul(s[1], inverse);
    d[2] = Vu_FMul(s[2], inverse);
    d[3] = w;
}

void sceVu0CopyVector(void *dst, void *src) {
    memmove(dst, src, sizeof(Vec));
}

/* vmul.xyz, vaddy.x, vaddz.x: (x*x' + y*y') + z*z' */
float sceVu0InnerProduct(void *a, void *b) {
    float *x = a, *y = b;
    float sum = Vu_FAdd(Vu_FMul(x[0], y[0]), Vu_FMul(x[1], y[1]));

    return Vu_FAdd(sum, Vu_FMul(x[2], y[2]));
}

/* vopmula.xyz ACC, a, b; vopmsub.xyz r, b, a: ACC.x = a.y*b.z, r.x = ACC.x - b.y*a.z, and so round;
   vsub.w r, r, r: w is +0 */
void sceVu0OuterProduct(void *dst, void *a, void *b) {
    float *x = a, *y = b;
    Vec acc, r;

    acc[0] = Vu_FMul(x[1], y[2]);
    acc[1] = Vu_FMul(x[2], y[0]);
    acc[2] = Vu_FMul(x[0], y[1]);
    r[0] = Vu_FSub(acc[0], Vu_FMul(y[1], x[2]));
    r[1] = Vu_FSub(acc[1], Vu_FMul(y[2], x[0]));
    r[2] = Vu_FSub(acc[2], Vu_FMul(y[0], x[1]));
    r[3] = 0.0f;
    memcpy(dst, r, sizeof(r));
}

/* vmul.xyz, vaddy.x, vaddz.x: the squared length; vsqrt Q (of its |x|); vaddq.x from vf0.x; vdiv Q,
   vf0w by that; vsub.xyzw r = vf0 - vf0 (all +0); vmulq.xyz r = src * Q: w is +0 */
void sceVu0Normalize(void *dst, void *src) {
    float *s = src;
    float sum = Vu_FAdd(Vu_FMul(s[0], s[0]), Vu_FMul(s[1], s[1]));
    float length = Vu_FAdd(0.0f, Vu_FSqrt(Vu_FAdd(sum, Vu_FMul(s[2], s[2]))));
    float scale = Vu_FDiv(1.0f, length);
    Vec r;

    r[0] = Vu_FMul(s[0], scale);
    r[1] = Vu_FMul(s[1], scale);
    r[2] = Vu_FMul(s[2], scale);
    r[3] = 0.0f;
    memcpy(dst, r, sizeof(r));
}

/* vmulax, vmadday, vmaddaz, vmaddw (xyzw): ACC = m0*x, ACC += m1*y, ACC += m2*z, r = ACC + m3*w, each
   product rounded before its sum */
void sceVu0ApplyMatrix(void *dst, void *m, void *v) {
    float (*a)[4] = m;
    float *x = v;
    Vec r;
    float acc;
    int i;

    for (i = 0; i < 4; i++) {
        acc = Vu_FMul(a[0][i], x[0]);
        acc = Vu_FAdd(acc, Vu_FMul(a[1][i], x[1]));
        acc = Vu_FAdd(acc, Vu_FMul(a[2][i], x[2]));
        r[i] = Vu_FAdd(acc, Vu_FMul(a[3][i], x[3]));
    }
    memcpy(dst, r, sizeof(r));
}

/* m0 applied to each row of m1 (the SDK loads m0 first, and reads each row of m1 before it stores that
   row of dst, so a dst that is either operand gives what a copy does) */
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

/* The inverse of a rotation and translation: the 3x3 rotation transposed (the EE's pextlw/pcpyld, with
   the w of rows 0-2 from vsub.xyz t - t: +0), and row 3 = (vf0 - (r0*t.x + r1*t.y + r2*t.z), t.w), the
   sum vmulax/vmadday/vmaddz.xyz, then vsub.xyz from the zeros. */
void sceVu0InversMatrix(void *dst, void *src) {
    float (*s)[4] = src;
    Mat r;
    float acc;
    int i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            r[i][j] = s[j][i];
        }
        r[i][3] = 0.0f;
    }
    for (i = 0; i < 3; i++) {
        acc = Vu_FMul(r[0][i], s[3][0]);
        acc = Vu_FAdd(acc, Vu_FMul(r[1][i], s[3][1]));
        r[3][i] = Vu_FSub(0.0f, Vu_FAdd(acc, Vu_FMul(r[2][i], s[3][2])));
    }
    r[3][3] = s[3][3];
    memcpy(dst, r, sizeof(r));
}

/* libvu0's _sceVu0ecossin, after the RotMatrix calls' EE code has turned the angle into
   t = pi/2 - |angle| (add.s/sub.s, host floats): cos(angle) as sin(t) by an odd polynomial to t^9, and
   sin(angle) as the square root of |1 - cos^2| with the sign of the angle, instruction by instruction
   (vmr32.w, vaddx.x, vmul.x ...). Exact within [-pi, pi]; outside it the polynomial drifts as retail's
   does, and the game relies on retail. */
static void ecossin(float angle, float *cos_out, float *sin_out) {
    static const float coeff[4] = {2.601887e-06f, -0.00019807414f, 0.0083330255f, -0.16666657f};
    const float half_pi = 1.57079637f; /* 0x3FC90FDB */
    int negative = angle < 0.0f;
    float t = negative ? half_pi + angle : half_pi - angle;
    float t2, sum, root;
    float term[4];
    int i;

    sum = Vu_FAdd(0.0f, t);                 /* vaddx.x vf4, vf0, vf6x (vf6.w = t from vmr32.w) */
    t2 = Vu_FMul(t, t);                     /* vmul.x vf6 */
    for (i = 0; i < 4; i++) {
        term[i] = Vu_FMul(coeff[i], t);     /* vmulw.xyzw vf8, vf5, vf6w */
    }
    for (i = 0; i < 4; i++) {
        term[i] = Vu_FMul(term[i], t2);     /* vmulx.xyzw */
    }
    for (i = 0; i < 3; i++) {
        term[i] = Vu_FMul(term[i], t2);     /* vmulx.xyz */
    }
    sum = Vu_FAdd(sum, term[3]);            /* vaddw.x */
    term[0] = Vu_FMul(term[0], t2);         /* vmulx.xy */
    term[1] = Vu_FMul(term[1], t2);
    sum = Vu_FAdd(sum, term[2]);            /* vaddz.x */
    term[0] = Vu_FMul(term[0], t2);         /* vmulx.x */
    sum = Vu_FAdd(sum, term[1]);            /* vaddy.x */
    sum = Vu_FAdd(sum, term[0]);            /* vaddx.x */
    sum = Vu_FAdd(0.0f, sum);               /* vaddx.xy vf4, vf5 (zeros), vf4x */
    root = Vu_FSub(1.0f, Vu_FMul(sum, sum)); /* vmul.x vf7; vsubx.w vf7, vf0, vf7x */
    root = Vu_FAdd(0.0f, Vu_FSqrt(root));   /* vsqrt Q, vf7w; vaddq.x vf7, vf0, Q */
    *cos_out = sum;
    *sin_out = negative ? Vu_FSub(0.0f, root) : Vu_FAdd(0.0f, root); /* vsubx.x / vaddx.x from vf5 */
}

/* The rotation matrix the SDK builds in vf6-vf9 from the unit rows (the ones vaddw from vf0w, the
   zeros vsub, vmove or vmr32 of zeros), the cosine and sine put in with vaddx/vaddy/vsubx from the zeros
   of vf5, then applied to each row of src (sceVu0MulMatrix's loop) */
static void rotate(void *dst, void *src, int axis, float angle) {
    Mat rot;
    float c, s;
    int a = (axis + 1) % 3, b = (axis + 2) % 3;

    ecossin(angle, &c, &s);

    sceVu0UnitMatrix(rot);
    rot[a][a] = Vu_FAdd(0.0f, c);
    rot[a][b] = Vu_FAdd(0.0f, s);
    rot[b][a] = Vu_FSub(0.0f, s);
    rot[b][b] = Vu_FAdd(0.0f, c);
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

/* rows 0-2 copied (lq/sq); row 3: vadd.xyz with the translation, its w kept */
void sceVu0TransMatrix(void *dst, void *src, void *translation) {
    float (*s)[4] = src;
    float (*d)[4] = dst;
    float *t = translation;
    Vec row;

    row[0] = Vu_FAdd(s[3][0], t[0]);
    row[1] = Vu_FAdd(s[3][1], t[1]);
    row[2] = Vu_FAdd(s[3][2], t[2]);
    row[3] = s[3][3];
    if (dst != src) {
        memmove(d, s, 3 * sizeof(Vec));
    }
    memcpy(d[3], row, sizeof(row));
}

/* The SDK's EE C: q and p in host floats, in its order; then two matrices multiplied on VU0 */
void sceVu0ViewScreenMatrix(void *dst, float scrz, float ax, float ay, float cx, float cy, float zmin,
                            float zmax, float nearz, float farz) {
    float *m = dst;
    float t[16];
    float p;
    float q;

    q = ((-zmax) * nearz + zmin * farz) / (-nearz + farz);
    p = ((farz * nearz) * (-zmin + zmax)) / (-nearz + farz);

    sceVu0UnitMatrix(m);
    m[0] = scrz;
    m[5] = scrz;
    m[10] = 0.0f;
    m[15] = 0.0f;
    m[14] = 1.0f;
    m[11] = 1.0f;

    sceVu0UnitMatrix(t);
    t[0] = ax;
    t[5] = ay;
    t[10] = p;
    t[12] = cx;
    t[13] = cy;
    t[14] = q;
    sceVu0MulMatrix(m, t, m);
}
