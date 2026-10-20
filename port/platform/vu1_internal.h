/* VU1's state and float arithmetic, shared by the interpreter (port/platform/vu1.c) and the
   high-level version of program 0x000 (port/platform/vu1_hle.c): the two must do the same arithmetic
   to the bit, so they use the same helpers. Private to those two files.

   Include it from a file compiled with -frounding-math: the helpers round as the current rounding
   mode says, which vu1.c sets to toward zero around a run. */
#ifndef PORT_VU1_INTERNAL_H
#define PORT_VU1_INTERNAL_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
/* vu1.c's run sets the SSE unit's denormals-are-zero (DAZ): IEEE's operands then read as the VU's, a
   denormal as +-0, without masking them */
#define VU_DAZ 1
#define MXCSR_DAZ 0x0040u
#else
#define VU_DAZ 0
#endif

#define MICRO_INSNS 2048 /* 16 KB of micro memory */
#define DATA_QWORDS 1024 /* 16 KB of data memory */
#define REG_ACC 32       /* VF write target: the accumulator */
#define REG_SCRATCH 33   /* where a write that goes nowhere goes */
#define REG_NONE 0xFF
#define VU_MAX 0x7FFFFFFFu /* the largest VU float */
#define SIGN 0x80000000u

enum { A_ADD, A_SUB, A_MUL, A_MADD, A_MSUB, A_MAX, A_MINI };

/* A flag write in flight, seen when it is due. */
enum { FW_FMAC, FW_CLIP, FW_STICKY };
typedef struct {
    uint64_t due;
    uint8_t kind;
    uint16_t mac;   /* FW_FMAC: the MAC flag */
    uint32_t value; /* FW_FMAC: Z S U O; FW_CLIP: the clip flag; FW_STICKY: FSSET's sticky bits */
} FlagWrite;

/* A write to a VI register, for the branches that read it (see the top of vu1.c) */
typedef struct {
    uint64_t cycle;    /* when the instruction issued */
    uint8_t reg;
    uint8_t flag_read; /* an FCxxx, FMxxx or FSxxx */
    uint16_t old;      /* the value before */
} ViWrite;

typedef struct {
    uint32_t vf[34][4]; /* vf00..vf31, ACC (REG_ACC), and REG_SCRATCH */
    uint16_t vi[16];
    uint32_t q, p, i, r;
    uint32_t q_next, p_next;
    uint64_t q_due, p_due;
    int q_pending, p_pending;
    uint32_t q_status_next; /* the divide's I and D bits */
    uint32_t mac;           /* the MAC flag the flag instructions see */
    uint32_t status;        /* the status flag they see */
    uint32_t clip;          /* every CLIP and FCSET so far */
    uint32_t clip_visible;  /* the clip flag they see */
    FlagWrite flag_queue[16]; /* in issue order, so in due order */
    unsigned flag_head, flag_count;
    int32_t mac_queued; /* the MAC flag of the last FMAC write queued, -1 after an FSSET */
    uint64_t next_due;     /* the earliest cycle settle() has something to do (Q, P, a flag write) */
    ViWrite vi_writes[8]; /* the last few, the newest at vi_write_count - 1 */
    unsigned vi_write_count;
    uint64_t cycle;
    uint32_t pc;           /* where MSCNT continues */
    int decoded;           /* micro memory decoded since the last upload */
    int flags_read;        /* micro memory has an FSxxx or FMxxx (see vu1.c's interpret) */
} Vu1State;

/* ---- Floats --------------------------------------------------------------------------------- */
/* The MAC flag bits of the w field (x is << 3) that vf_round reports; S is the result's sign */
#define FL_Z 0x0001u
#define FL_U 0x0100u
#define FL_O 0x1000u

/* A VU float as a double, exactly: a denormal is +-0, and an exponent of 255 is a number. */
static inline double vf_value(uint32_t bits) {
    uint32_t e = (bits >> 23) & 0xFF;
    uint64_t d;
    double v;

    if (e == 0) {
        d = (uint64_t)(bits & SIGN) << 32;
    } else {
        d = ((uint64_t)(bits & SIGN) << 32) | ((uint64_t)(e + 896) << 52) |
            ((uint64_t)(bits & 0x007FFFFFu) << 29);
    }
    memcpy(&v, &d, 8);
    return v;
}

/* A result (already rounded toward zero in double) as a VU float, truncated to 24 bits; adds its Z,
   U and O flags (FL_*) to *flags if asked for. */
static inline uint32_t vf_round(double v, unsigned *flags) {
    uint64_t d;
    uint32_t sign;
    int e;
    unsigned fl = 0;

    memcpy(&d, &v, 8);
    sign = (uint32_t)(d >> 32) & SIGN;
    e = (int)((d >> 52) & 0x7FF) - 896;
    if (v == 0.0) {
        fl = FL_Z;
        d = sign;
    } else if (e <= 0) {
        fl = FL_U | FL_Z;
        d = sign;
    } else if (e > 255) {
        fl = FL_O;
        d = sign | VU_MAX;
    } else {
        d = sign | ((uint32_t)e << 23) | (uint32_t)((d >> 29) & 0x007FFFFFu);
    }
    if (flags) {
        *flags |= fl;
    }
    return (uint32_t)d;
}

/* a + b, a - b or a * b (A_ADD, A_SUB, A_MUL), with its Z, U and O flags added to *flags. The common
   case is done in IEEE floats, rounding toward zero as the rest: operands below exponent 255 and a
   result that is neither zero, a denormal nor the largest IEEE float (where IEEE stops and the VU
   goes on) give the same bits. Everything else is done again exactly. */
static inline uint32_t vf_op(int op, uint32_t a, uint32_t b, unsigned *flags) {
    if ((a & 0x7F800000u) != 0x7F800000u && (b & 0x7F800000u) != 0x7F800000u) {
        uint32_t fa = (a & 0x7F800000u) ? a : a & SIGN, fb = (b & 0x7F800000u) ? b : b & SIGN;
        float x, y, r;
        uint32_t bits;

        memcpy(&x, &fa, 4);
        memcpy(&y, &fb, 4);
        r = op == A_ADD ? x + y : op == A_SUB ? x - y : x * y;
        memcpy(&bits, &r, 4);
        if ((bits & 0x7F800000u) != 0 && (bits & 0x7FFFFFFFu) != 0x7F7FFFFFu) {
            return bits;
        }
    }
    if (op == A_ADD) {
        return vf_round(vf_value(a) + vf_value(b), flags);
    }
    if (op == A_SUB) {
        return vf_round(vf_value(a) - vf_value(b), flags);
    }
    return vf_round(vf_value(a) * vf_value(b), flags);
}

/* ---- Four fields at once (GCC's vector extensions) ------------------------------------------ */
typedef uint32_t V4 __attribute__((vector_size(16)));
typedef int32_t V4i __attribute__((vector_size(16)));
typedef float V4f __attribute__((vector_size(16)));

/* a field mask (x = 8 .. w = 1) as lanes of all ones */
static const V4 field_lanes[16] = {
    {0, 0, 0, 0}, {0, 0, 0, ~0u}, {0, 0, ~0u, 0}, {0, 0, ~0u, ~0u},
    {0, ~0u, 0, 0}, {0, ~0u, 0, ~0u}, {0, ~0u, ~0u, 0}, {0, ~0u, ~0u, ~0u},
    {~0u, 0, 0, 0}, {~0u, 0, 0, ~0u}, {~0u, 0, ~0u, 0}, {~0u, 0, ~0u, ~0u},
    {~0u, ~0u, 0, 0}, {~0u, ~0u, 0, ~0u}, {~0u, ~0u, ~0u, 0}, {~0u, ~0u, ~0u, ~0u}};

static inline V4 v4_load(const uint32_t *p) {
    V4 v;

    memcpy(&v, p, 16);
    return v;
}

static inline void v4_store(uint32_t *p, V4 v) {
    memcpy(p, &v, 16);
}

static inline V4 v4_splat(uint32_t x) {
    return (V4){x, x, x, x};
}

static inline int v4_any(V4 v) {
    uint64_t h[2];

    memcpy(h, &v, 16);
    return (h[0] | h[1]) != 0;
}

/* vf_op on the lanes set in `lanes` (the others are left undefined), with their flags added to *flags
   (x << 3 ... w) if flags: four at once in IEEE floats as vf_op's fast path, and vf_op itself for a
   lane where the VU and IEEE part (an operand of exponent 255, or a result at the largest IEEE float).
   An operand of exponent 255 is an infinity or a NaN to IEEE, and so is the result, so the result
   alone tells. A result of exponent 0 is done here too: its value is +-0 with IEEE's sign, which is
   the exact result's, as vf_round's; it sets Z, and U when the exact result was not zero. A sum of
   two floats is a multiple of 2^-149, so it is zero exactly when IEEE's is; a product, when an
   operand is. */
static inline V4 v4_op(int op, V4 a, V4 b, V4 lanes, unsigned *flags) {
    const V4 exp = v4_splat(0x7F800000u);
    V4 r, magnitude, slow, zero;
#if VU_DAZ
    V4f x = (V4f)a, y = (V4f)b;
#else
    V4f x = (V4f)(a & ((V4)((a & exp) != 0) | SIGN)), y = (V4f)(b & ((V4)((b & exp) != 0) | SIGN));
#endif
    int f;

    r = (V4)(op == A_ADD ? x + y : op == A_SUB ? x - y : x * y);
    magnitude = r & 0x7FFFFFFFu;
    slow = (V4)((V4i)magnitude >= 0x7F7FFFFF) & lanes;
    zero = (V4)((V4i)magnitude < 0x00800000) & lanes & ~slow;
    if (flags && v4_any(zero)) {
        V4 under = zero & (op == A_MUL ? (V4)((a & exp) != 0) & (V4)((b & exp) != 0) : (V4)(magnitude != 0));
        V4 bits = (zero & (V4){FL_Z << 3, FL_Z << 2, FL_Z << 1, FL_Z}) |
                  (under & (V4){FL_U << 3, FL_U << 2, FL_U << 1, FL_U});

        *flags |= bits[0] | bits[1] | bits[2] | bits[3];
    }
    r &= ~zero | SIGN;
    if (v4_any(slow)) {
        for (f = 0; f < 4; f++) {
            if (slow[f]) {
                unsigned fl = 0;

                r[f] = vf_op(op, a[f], b[f], &fl);
                if (flags) {
                    *flags |= fl << (3 - f);
                }
            }
        }
    }
    return r;
}

/* vf_max and vf_mini, four at once */
static inline V4 v4_max(V4 a, V4 b) {
    V4i sa = (V4i)a, sb = (V4i)b;
    V4 both_negative = (V4)((sa < 0) & (sb < 0));
    V4 take_a = (both_negative & (V4)(sa < sb)) | (~both_negative & (V4)(sa > sb));

    return (a & take_a) | (b & ~take_a);
}

static inline V4 v4_mini(V4 a, V4 b) {
    V4 a_is_max = (V4)(v4_max(a, b) == a);

    return (b & a_is_max) | (a & ~a_is_max);
}

static inline uint32_t vf_add(uint32_t a, uint32_t b) {
    return vf_op(A_ADD, a, b, NULL);
}

static inline uint32_t vf_sub(uint32_t a, uint32_t b) {
    return vf_op(A_SUB, a, b, NULL);
}

static inline uint32_t vf_mul(uint32_t a, uint32_t b) {
    return vf_op(A_MUL, a, b, NULL);
}

static inline int vf_is_zero(uint32_t bits) {
    return (bits & 0x7F800000u) == 0;
}

/* a / b; by zero, the largest float with the quotient's sign (0/0 too) */
static inline uint32_t vf_div(uint32_t a, uint32_t b) {
    if (vf_is_zero(b)) {
        return ((a ^ b) & SIGN) | VU_MAX;
    }
    return vf_round(vf_value(a) / vf_value(b), NULL);
}

/* the square root of |a| */
static inline uint32_t vf_sqrt(uint32_t a) {
    return vf_round(sqrt(fabs(vf_value(a))), NULL);
}

static inline uint32_t f32_bits(float f) {
    uint32_t bits;

    memcpy(&bits, &f, 4);
    return bits;
}

/* MAX and MINI compare sign and magnitude, so -0 < +0, and return an operand unchanged */
static inline uint32_t vf_max(uint32_t a, uint32_t b) {
    if ((int32_t)a < 0 && (int32_t)b < 0) {
        return (int32_t)a < (int32_t)b ? a : b;
    }
    return (int32_t)a > (int32_t)b ? a : b;
}

static inline uint32_t vf_mini(uint32_t a, uint32_t b) {
    return vf_max(a, b) == a ? b : a;
}

/* FTOIn of four fields: each VU float times 2^shift, truncated, or the integer's limit with its
   sign at 2^31 and beyond. Below that, IEEE floats scale it exactly (a power of two, no overflow)
   and truncate it as the VU does; a denormal is +-0. */
static inline V4 v4_ftoi(V4 a, int shift) {
    V4 e = a & 0x7F800000u;
    V4 big = (V4)(e >= (uint32_t)(158 - shift) << 23); /* |a| >= 2^(31 - shift) */
    V4f x = (V4f)(a & ((V4)(e != 0) | SIGN) & ~big);
    V4 r = (V4)__builtin_convertvector(x * (float)(1 << shift), V4i);

    return (r & ~big) | (((a >> 31) + 0x7FFFFFFFu) & big);
}

/* ITOF0/4/12/15 and FTOI0/4/12/15: the fraction bits of the fixed-point side */
static const int fixed_shift[4] = {0, 4, 12, 15};

/* ITOFn of four fields: each integer as a float rounded toward zero, over 2^shift. The IEEE
   conversion rounds as the VU does (in the current rounding mode), and the scale is exact. */
static inline V4 v4_itof(V4 a, int shift) {
    V4f x = __builtin_convertvector((V4i)a, V4f);

    return (V4)(x * (1.0f / (float)(1 << shift)));
}

/* The MAC flag's Z S U O, as the status flag has them */
static inline unsigned mac_zsuo(unsigned mac) {
    return ((mac & 0x000F) ? 1u : 0) | ((mac & 0x00F0) ? 2u : 0) | ((mac & 0x0F00) ? 4u : 0) |
           ((mac & 0xF000) ? 8u : 0);
}

/* ---- Between the two ------------------------------------------------------------------------ */
/* vu1.c: sends the GIF packet at a VU1 address down PATH1, as XGKICK does (outside the run's DAZ). */
void vu1_xgkick(uint32_t address);

/* vu1_hle.c: 1 when micro memory holds the game's program 0x000 (checked when it is decoded). */
int vu1_hle_recognise(const uint8_t *micro);

/* vu1_hle.c: runs program 0x000 from pc (0x000, MSCAL, or 0x11F, MSCNT) on the state and data
   memory, as the interpreter would to the bit, and returns the instruction pairs it would have run;
   0 when it leaves this run to the interpreter (the state is then untouched). Called with the
   rounding mode and DAZ set as for the interpreter. */
uint64_t vu1_hle_run(Vu1State *vu, uint8_t *data, uint32_t pc, uint32_t top);

/* vu1_hle_280.c: the same for program 0x280 (MSCAL 0x280, MSCNT 0x4AA) */
int vu1_hle_280_recognise(const uint8_t *micro);
uint64_t vu1_hle_280_run(Vu1State *vu, uint8_t *data, uint32_t pc, uint32_t top);

#endif
