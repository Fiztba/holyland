/* What the HLEs of VU1's programs share (vu1_hle.c, vu1_hle_280.c): the FMAC arithmetic in its exact
   and fast forms (vu1_hle.c's top comment says when the fast one gives the same bits), and data
   memory. Private to them; include after vu1_internal.h. */
#ifndef PORT_VU1_HLE_OPS_H
#define PORT_VU1_HLE_OPS_H

#include "vu1_internal.h"

#define LANES_ALL field_lanes[15]
#define LANES_XYZ field_lanes[14]
#define LANES_W field_lanes[1]

#define ALWAYS_INLINE inline __attribute__((always_inline))

#if VU_DAZ
#define MXCSR_FTZ 0x8000u
#define MXCSR_FLAGS 0x003Fu
#define MXCSR_TROUBLE 0x0019u /* invalid, overflow, underflow */
#endif

/* FNV-1a 64 of micro memory's pairs first .. first + pairs - 1: how an HLE recognises its program */
static inline uint64_t hle_micro_hash(const uint8_t *micro, unsigned first, unsigned pairs) {
    uint64_t hash = 0xCBF29CE484222325ull;
    unsigned n;

    for (n = first * 8; n < (first + pairs) * 8; n++) {
        hash = (hash ^ micro[n]) * 0x100000001B3ull;
    }
    return hash;
}

/* ---- Arithmetic ------------------------------------------------------------------------------ */
/* The flags the run's FMAC instructions raise, as the interpreter keeps them when nothing in micro
   memory reads them (vu1.c's interpret): Z U O of them all, and the signs of their results. */
typedef struct {
    unsigned zuo;
    V4 signs;
} Flags;

/* a op b on the fields in lanes (the others are undefined): exact, the interpreter's, with its
   flags; or fast (see vu1_hle.c's top) */
static ALWAYS_INLINE V4 fop(Flags *fl, int exact, int op, V4 a, V4 b, V4 lanes) {
    if (exact) {
        V4 r = v4_op(op, a, b, lanes, &fl->zuo);

        fl->signs |= r & lanes;
        return r;
    } else {
        V4f x = (V4f)a, y = (V4f)b;

        return (V4)(op == A_ADD ? x + y : op == A_SUB ? x - y : x * y);
    }
}

/* MADDx: acc + a * b, the product rounded first (and its flags not kept) */
static ALWAYS_INLINE V4 fmadd(Flags *fl, int exact, V4 acc, V4 a, V4 b, V4 lanes) {
    V4 product = exact ? v4_op(A_MUL, a, b, lanes, NULL) : (V4)((V4f)a * (V4f)b);

    return fop(fl, exact, A_ADD, acc, product, lanes);
}

/* M * v, as MULAx ACC; MADDAy; MADDAz; MADDw */
static ALWAYS_INLINE V4 transform4(Flags *fl, int exact, const V4 *m, V4 v) {
    V4 acc = fop(fl, exact, A_MUL, m[0], v4_splat(v[0]), LANES_ALL);

    acc = fmadd(fl, exact, acc, m[1], v4_splat(v[1]), LANES_ALL);
    acc = fmadd(fl, exact, acc, m[2], v4_splat(v[2]), LANES_ALL);
    return fmadd(fl, exact, acc, m[3], v4_splat(v[3]), LANES_ALL);
}

/* x*x + y*y + z*z, as ESADD (vu1.c's efu_sum_squares) */
static ALWAYS_INLINE uint32_t sum_squares(int exact, V4 v) {
    if (exact) {
        return vf_add(vf_add(vf_mul(v[0], v[0]), vf_mul(v[1], v[1])), vf_mul(v[2], v[2]));
    } else {
        V4f f = (V4f)v;
        float xx = f[0] * f[0], yy = f[1] * f[1], zz = f[2] * f[2];
        float sum = xx + yy;

        sum = sum + zz;
        return f32_bits(sum);
    }
}

/* a with b's lanes */
static inline V4 merge(V4 a, V4 b, V4 lanes) {
    return (a & ~lanes) | (b & lanes);
}

static inline V4 splat(V4 v, int f) {
    return v4_splat(v[f]);
}

/* any field with exponent 255 (a number to the VU, not to SSE) */
static inline int has_huge(V4 v) {
    return v4_any((V4)((v & 0x7F800000u) == 0x7F800000u));
}

/* CLIP's judgement of xyz against |w| (vu1.c's U_CLIP): +x -x +y -y +z -z in bits 0-5 */
static inline uint32_t judge(V4 v, uint32_t w) {
    uint32_t j = 0;
    int f;

    w &= 0x7FFFFFFFu;
    if ((w & 0x7F800000u) == 0) {
        w = 0x007FFFFFu;
    }
    for (f = 0; f < 3; f++) {
        if ((v[f] & 0x7FFFFFFFu) > w) {
            j |= (v[f] & SIGN ? 2u : 1u) << (f * 2);
        }
    }
    return j;
}

/* FCAND vi01, 0x3E0 on the clip flag after a vertex's two CLIPs (guard band, then 0): outside the
   guard band in x or y, or z < 0 */
static inline int clipped(uint32_t judgements) {
    return (judgements & 0x3E0u) != 0;
}

/* DIV's status bits: D for x / 0, I for 0 / 0 */
static inline uint32_t div_status(uint32_t s, uint32_t t) {
    return !vf_is_zero(t) ? 0 : vf_is_zero(s) ? 0x10u : 0x20u;
}

static inline uint32_t *qword(uint8_t *data, uint32_t address) {
    return (uint32_t *)(data + (address & (DATA_QWORDS - 1)) * 16);
}

static inline V4 load(uint8_t *data, uint32_t address) {
    return v4_load(qword(data, address));
}

static inline void store(uint8_t *data, uint32_t address, V4 v) {
    v4_store(qword(data, address), v);
}

#endif
