/* VU1 on the CPU: an interpreter for the microprograms the game uploads (game/data/vudata.c; see
   tools/vu1_disasm.py for their text). It knows every VU1 instruction; an encoding that is none of
   them stops the program (PORT_FATAL), as does an instruction the game's programs could not run.

   The game's microprograms are software-pipelined: they issue an instruction and use its result a
   fixed number of cycles later, and in places they rely on reading the old value before a result is
   due. So this counts cycles as VU1 does (one instruction pair a cycle, plus the stalls the hardware
   inserts) and makes each result visible when VU1 would:
   - VF registers: an FMAC result (upper instructions, and LQ, MOVE, MR32, MFIR, MFP, RGET, RNEXT) is
     due 4 cycles after issue. A read before then stalls until it is due, as on VU1, so the value
     read is always the new one; only the cycle count moves.
   - VI registers: ILW and ILWR are due 4 cycles after issue, and an instruction that reads the
     register before then stalls. Every other VI write is immediate, but see the branches below.
   - Q: DIV and SQRT are due 7 cycles after issue, RSQRT 13. Until then Q keeps its old value (there
     is no stall), WAITQ waits for it, and a second divide waits for the first. The divide's I and D
     status bits change when Q does.
   - P: each EFU instruction has its own latency (ESADD 11 ... EATAN 54). MFP reads the old P until
     the result is due. WAITP and the next EFU instruction stall until one cycle before it is due,
     and P is written then.
   - The flags: the MAC and status flags an FMAC instruction sets, the clip flag a CLIP or FCSET
     sets, and the sticky bits FSSET sets are seen by FMxxx, FSxxx, FCxxx 4 cycles after issue.
   Within a pair the upper and the lower instruction both read their operands before either writes;
   when both write the same VF register the lower one's write is dropped. The I register an upper
   instruction reads is the one before its own pair's LOI.

   Floats are VU floats, not IEEE ones. An exponent of 255 is an ordinary exponent, so there are no
   infinities or NaNs and the largest value is 0x7FFFFFFF (about 2^129); denormals read as zero.
   Arithmetic rounds toward zero. A result too large is +-0x7FFFFFFF and sets O, one too small is
   +-0 and sets U (and Z). Division by zero gives +-0x7FFFFFFF and sets D (I for 0/0). The arithmetic
   is IEEE's rounded toward zero; the PS2's adder can differ from that in the last bit, which is not
   modelled.

   Branches: a conditional branch reads its VI registers early. When the instruction just before it
   wrote one of them with an integer op (IADD ... ISUBIU, LQI/LQD/SQI/SQD's step, MTIR, XTOP,
   XITOP), the branch sees the value the register had 4 cycles before the branch, before every
   write in that window (PCSX2's microVU, after hardware tests). The flag instructions' writes to a
   VI register, and an ILW (which stalls the branch), are seen at once. Of the game's programs only
   the one at 0x700 relies on it (its loop at 0x7BE).

   Only the DMA controller's thread calls in here (port/platform/dmac.c). */
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
/* run() sets the SSE unit's denormals-are-zero (DAZ): IEEE's operands then read as the VU's, a
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

enum {
    /* upper: the generic arithmetic ones (op, second operand, destination) */
    U_NOP, U_ITOF, U_FTOI, U_ABS, U_CLIP, U_OPMULA, U_OPMSUB, U_UNKNOWN,
    U_ARITH, /* + arith * 4 + bsrc (decode_upper), so that one jump picks both */
};
enum { A_ADD, A_SUB, A_MUL, A_MADD, A_MSUB, A_MAX, A_MINI };
enum { B_FT, B_BC, B_Q, B_I };

enum {
    L_NOP, L_LOI, L_LQ, L_SQ, L_LQI, L_LQD, L_SQI, L_SQD, L_ILW, L_ISW, L_ILWR, L_ISWR,
    L_IADDIU, L_ISUBIU, L_IADD, L_ISUB, L_IADDI, L_IAND, L_IOR,
    L_MOVE, L_MR32, L_MTIR, L_MFIR, L_DIV, L_SQRT, L_RSQRT, L_WAITQ,
    L_FCAND, L_FCSET, L_FCEQ, L_FCOR, L_FCGET, L_FSAND, L_FSSET, L_FSEQ, L_FSOR, L_FMAND, L_FMEQ,
    L_FMOR,
    L_B, L_BAL, L_JR, L_JALR, L_IBEQ, L_IBNE, L_IBLTZ, L_IBGTZ, L_IBLEZ, L_IBGEZ,
    L_XTOP, L_XITOP, L_XGKICK, L_MFP, L_WAITP,
    /* the EFU, in the order of efu_latency */
    L_ESADD, L_ERSADD, L_ELENG, L_ERLENG, L_EATANXY, L_EATANXZ, L_ESUM, L_ESQRT, L_ERSQRT,
    L_ERCPR, L_ESIN, L_EATAN, L_EEXP,
    L_RINIT, L_RGET, L_RNEXT, L_RXOR,
    L_UNKNOWN,
};
#define IS_EFU(lop) ((lop) >= L_ESADD && (lop) <= L_EEXP)
/* cycles from issue until P has the result (the VU1 manual's EFU table) */
static const uint8_t efu_latency[] = {11, 18, 18, 24, 54, 54, 12, 12, 18, 12, 29, 54, 44};

typedef struct {
    uint8_t reg;  /* VF register */
    uint8_t mask; /* fields (x = 8 .. w = 1) */
} VfUse;

typedef struct {
    /* upper */
    uint8_t uop, arith, bsrc, bc, udest, fd, fs, ft;
    uint8_t uwrite; /* VF register written (REG_ACC, REG_NONE) */
    uint8_t uflags; /* sets the MAC and status flags */
    /* lower */
    uint8_t lop, ldest, lt, ls, ld, fsf, ftf;
    uint8_t lwrite; /* VF register written (REG_NONE) */
    uint16_t vireads; /* VI registers read (bit n: vi n), for ILW's stall */
    int32_t imm;
    uint32_t i_value; /* LOI */
    uint8_t ebit;
    uint8_t qwait; /* DIV, SQRT, RSQRT, WAITQ: waits for a divide in flight */
    uint8_t pwait; /* the EFU's and WAITP: wait for P */
    /* the write-back, worked out at decode: the register each result goes to (vf01..vf31, REG_ACC)
       and its fields, none if it writes nothing or vf00, or if it is the lower's and the upper
       writes the same register */
    uint8_t uwreg, uwmask, lwreg, lwmask;
    uint16_t ilw_write; /* ILW, ILWR: the VI register it loads (bit n: vi n), due 4 cycles on */
    uint8_t hazard; /* bit k - 1: it reads a VF field or VI register the instruction k before it in
                       memory writes with a latency (ilw_write, the VF writes) */
    uint8_t nreads;
    VfUse reads[4];
} VuInsn;

/* A flag write in flight, seen when it is due. */
enum { FW_FMAC, FW_CLIP, FW_STICKY };
typedef struct {
    uint64_t due;
    uint8_t kind;
    uint16_t mac;   /* FW_FMAC: the MAC flag */
    uint32_t value; /* FW_FMAC: Z S U O; FW_CLIP: the clip flag; FW_STICKY: FSSET's sticky bits */
} FlagWrite;

/* A write to a VI register, for the branches that read it (see the top) */
typedef struct {
    uint64_t cycle;    /* when the instruction issued */
    uint8_t reg;
    uint8_t flag_read; /* an FCxxx, FMxxx or FSxxx */
    uint16_t old;      /* the value before */
} ViWrite;

static struct {
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
    int flags_read;        /* micro memory has an FSxxx or FMxxx (see run) */
} vu;

static uint8_t *micro;    /* 0x11008000 */
static uint8_t *data;     /* 0x1100C000 */
static VuInsn code[MICRO_INSNS];

/* ---- Floats --------------------------------------------------------------------------------- */
/* The MAC flag bits of the w field (x is << 3) that vf_round reports; S is the result's sign */
#define FL_Z 0x0001u
#define FL_U 0x0100u
#define FL_O 0x1000u

/* A VU float as a double, exactly: a denormal is +-0, and an exponent of 255 is a number. */
static double vf_value(uint32_t bits) {
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
static uint32_t vf_round(double v, unsigned *flags) {
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

/* ---- Decoding ------------------------------------------------------------------------------- */
static void add_read(VuInsn *in, int reg, int mask) {
    if (reg == 0 || mask == 0) {
        return; /* vf00 is never written, so it never stalls */
    }
    in->reads[in->nreads].reg = (uint8_t)reg;
    in->reads[in->nreads].mask = (uint8_t)mask;
    in->nreads++;
}

static void decode_upper(VuInsn *in, uint32_t w) {
    int dest = (w >> 21) & 15;
    int ft = (w >> 16) & 31, fs = (w >> 11) & 31, fd = (w >> 6) & 31, op = w & 63;
    int bc = op & 3;
    static const uint8_t bc_arith[7] = {A_ADD, A_SUB, A_MADD, A_MSUB, A_MAX, A_MINI, A_MUL};
    static const struct { uint8_t arith, bsrc; } qi[12] = {
        {A_MUL, B_Q}, {A_MAX, B_I}, {A_MUL, B_I}, {A_MINI, B_I}, {A_ADD, B_Q}, {A_MADD, B_Q},
        {A_ADD, B_I}, {A_MADD, B_I}, {A_SUB, B_Q}, {A_MSUB, B_Q}, {A_SUB, B_I}, {A_MSUB, B_I}};
    static const uint8_t three[8] = {A_ADD, A_MADD, A_MUL, A_MAX, A_SUB, A_MSUB, 0xFF, A_MINI};

    in->udest = (uint8_t)dest;
    in->fd = (uint8_t)fd;
    in->fs = (uint8_t)fs;
    in->ft = (uint8_t)ft;
    in->bc = (uint8_t)bc;
    in->uwrite = REG_NONE;
    in->uop = U_ARITH;
    if (op < 0x1C) {
        in->arith = bc_arith[op >> 2];
        in->bsrc = B_BC;
        in->uwrite = (uint8_t)fd;
    } else if (op < 0x28) {
        in->arith = qi[op - 0x1C].arith;
        in->bsrc = qi[op - 0x1C].bsrc;
        in->uwrite = (uint8_t)fd;
    } else if (op < 0x30) {
        if (op == 0x2E) {
            in->uop = U_OPMSUB;
            in->uflags = 1;
            in->udest = 0xE;
            add_read(in, fs, 0xE);
            add_read(in, ft, 0xE);
            in->uwrite = (uint8_t)fd;
            return;
        }
        in->arith = three[op - 0x28];
        in->bsrc = B_FT;
        in->uwrite = (uint8_t)fd;
    } else if (op >= 0x3C) {
        int sop = (fd << 2) | bc;

        if (sop < 0x10 || (sop >= 0x18 && sop < 0x1C)) {
            static const uint8_t acc_bc[7] = {A_ADD, A_SUB, A_MADD, A_MSUB, 0, 0, A_MUL};

            in->arith = acc_bc[sop >> 2];
            in->bsrc = B_BC;
            in->uwrite = REG_ACC;
        } else if (sop >= 0x10 && sop < 0x18) {
            in->uop = sop < 0x14 ? U_ITOF : U_FTOI;
            in->bc = (uint8_t)(sop & 3); /* the scale */
            add_read(in, fs, dest);
            in->uwrite = (uint8_t)ft;
            return;
        } else if (sop == 0x1D) {
            in->uop = U_ABS;
            add_read(in, fs, dest);
            in->uwrite = (uint8_t)ft;
            return;
        } else if (sop == 0x1F) {
            in->uop = U_CLIP;
            add_read(in, fs, 0xE);
            add_read(in, ft, 0x1);
            return;
        } else if (sop == 0x2E) {
            in->uop = U_OPMULA;
            in->uflags = 1;
            add_read(in, fs, 0xE);
            add_read(in, ft, 0xE);
            in->uwrite = REG_ACC;
            in->udest = 0xE;
            return;
        } else if (sop == 0x2F) {
            in->uop = U_NOP;
            return;
        } else if (sop >= 0x1C && sop < 0x2E && sop != 0x2B) {
            static const struct { uint8_t arith, bsrc; } acc_qi[18] = {
                {A_MUL, B_Q}, {0, 0}, {A_MUL, B_I}, {0, 0}, {A_ADD, B_Q}, {A_MADD, B_Q},
                {A_ADD, B_I}, {A_MADD, B_I}, {A_SUB, B_Q}, {A_MSUB, B_Q}, {A_SUB, B_I}, {A_MSUB, B_I},
                {A_ADD, B_FT}, {A_MADD, B_FT}, {A_MUL, B_FT}, {0, 0}, {A_SUB, B_FT}, {A_MSUB, B_FT}};

            in->arith = acc_qi[sop - 0x1C].arith;
            in->bsrc = acc_qi[sop - 0x1C].bsrc;
            in->uwrite = REG_ACC;
        } else {
            in->uop = U_UNKNOWN;
            return;
        }
    } else {
        in->uop = U_UNKNOWN;
        return;
    }
    in->uflags = in->arith != A_MAX && in->arith != A_MINI;
    in->uop = (uint8_t)(U_ARITH + in->arith * 4 + in->bsrc);
    add_read(in, fs, dest);
    if (in->bsrc == B_FT) {
        add_read(in, ft, dest);
    } else if (in->bsrc == B_BC) {
        add_read(in, ft, 8 >> bc);
    }
}

static int32_t sext(uint32_t v, int bits) {
    return (int32_t)(v << (32 - bits)) >> (32 - bits);
}

static void decode_lower(VuInsn *in, uint32_t w) {
    int top = (w >> 25) & 0x7F;
    int dest = (w >> 21) & 15;
    int it = (w >> 16) & 31, is = (w >> 11) & 31, id = (w >> 6) & 31;
    uint16_t vs = (uint16_t)((1u << (is & 15)) & ~1u), vt = (uint16_t)((1u << (it & 15)) & ~1u);

    in->ldest = (uint8_t)dest;
    in->lt = (uint8_t)it;
    in->ls = (uint8_t)is;
    in->ld = (uint8_t)id;
    in->fsf = (uint8_t)((w >> 21) & 3);
    in->ftf = (uint8_t)((w >> 23) & 3);
    in->lwrite = REG_NONE;
    in->imm = sext(w & 0x7FF, 11);
    in->lop = L_UNKNOWN;
    if (top == 0x40) {
        int op = w & 63;

        switch (op) {
        case 0x30: in->lop = L_IADD; in->vireads = vs | vt; return;
        case 0x31: in->lop = L_ISUB; in->vireads = vs | vt; return;
        case 0x32: in->lop = L_IADDI; in->imm = sext(id, 5); in->vireads = vs; return;
        case 0x34: in->lop = L_IAND; in->vireads = vs | vt; return;
        case 0x35: in->lop = L_IOR; in->vireads = vs | vt; return;
        }
        if (op < 0x3C) {
            return;
        }
        switch ((id << 2) | (op & 3)) {
        case 0x30:
            in->lop = L_MOVE;
            add_read(in, is, dest);
            in->lwrite = (uint8_t)it;
            if (dest == 0 || it == 0) {
                in->lop = L_NOP;
                in->nreads = 0;
                in->lwrite = REG_NONE;
            }
            return;
        case 0x31:
            in->lop = L_MR32;
            add_read(in, is, ((dest & 8) ? 4 : 0) | ((dest & 4) ? 2 : 0) | ((dest & 2) ? 1 : 0) | ((dest & 1) ? 8 : 0));
            in->lwrite = (uint8_t)it;
            return;
        case 0x34: in->lop = L_LQI; in->lwrite = (uint8_t)it; in->vireads = vs; return;
        case 0x35: in->lop = L_SQI; add_read(in, is, dest); in->vireads = vt; return;
        case 0x36: in->lop = L_LQD; in->lwrite = (uint8_t)it; in->vireads = vs; return;
        case 0x37: in->lop = L_SQD; add_read(in, is, dest); in->vireads = vt; return;
        case 0x38: in->lop = L_DIV; add_read(in, is, 8 >> in->fsf); add_read(in, it, 8 >> in->ftf); return;
        case 0x39: in->lop = L_SQRT; add_read(in, it, 8 >> in->ftf); return;
        case 0x3A: in->lop = L_RSQRT; add_read(in, is, 8 >> in->fsf); add_read(in, it, 8 >> in->ftf); return;
        case 0x3B: in->lop = L_WAITQ; return;
        case 0x3C: in->lop = L_MTIR; add_read(in, is, 8 >> in->fsf); return;
        case 0x3D: in->lop = L_MFIR; in->lwrite = (uint8_t)it; in->vireads = vs; return;
        case 0x3E: in->lop = L_ILWR; in->vireads = vs; return;
        case 0x3F: in->lop = L_ISWR; in->vireads = vs | vt; return;
        case 0x40: in->lop = L_RNEXT; in->lwrite = (uint8_t)it; return;
        case 0x41: in->lop = L_RGET; in->lwrite = (uint8_t)it; return;
        case 0x42: in->lop = L_RINIT; add_read(in, is, 8 >> in->fsf); return;
        case 0x43: in->lop = L_RXOR; add_read(in, is, 8 >> in->fsf); return;
        case 0x64: in->lop = L_MFP; in->lwrite = (uint8_t)it; return;
        case 0x68: in->lop = L_XTOP; return;
        case 0x69: in->lop = L_XITOP; return;
        case 0x6C: in->lop = L_XGKICK; in->vireads = vs; return;
        case 0x70: in->lop = L_ESADD; add_read(in, is, 0xE); return;
        case 0x71: in->lop = L_ERSADD; add_read(in, is, 0xE); return;
        case 0x72: in->lop = L_ELENG; add_read(in, is, 0xE); return;
        case 0x73: in->lop = L_ERLENG; add_read(in, is, 0xE); return;
        case 0x74: in->lop = L_EATANXY; add_read(in, is, 0xC); return;
        case 0x75: in->lop = L_EATANXZ; add_read(in, is, 0xA); return;
        case 0x76: in->lop = L_ESUM; add_read(in, is, 0xF); return;
        case 0x78: in->lop = L_ESQRT; add_read(in, is, 8 >> in->fsf); return;
        case 0x79: in->lop = L_ERSQRT; add_read(in, is, 8 >> in->fsf); return;
        case 0x7A: in->lop = L_ERCPR; add_read(in, is, 8 >> in->fsf); return;
        case 0x7B: in->lop = L_WAITP; return;
        case 0x7C: in->lop = L_ESIN; add_read(in, is, 8 >> in->fsf); return;
        case 0x7D: in->lop = L_EATAN; add_read(in, is, 8 >> in->fsf); return;
        case 0x7E: in->lop = L_EEXP; add_read(in, is, 8 >> in->fsf); return;
        }
        return;
    }
    switch (top) {
    case 0x00: in->lop = L_LQ; in->lwrite = (uint8_t)it; in->vireads = vs; return;
    case 0x01: in->lop = L_SQ; add_read(in, is, dest); in->vireads = vt; return;
    case 0x04: in->lop = L_ILW; in->vireads = vs; return;
    case 0x05: in->lop = L_ISW; in->vireads = vs | vt; return;
    case 0x08: in->lop = L_IADDIU; in->imm = (int32_t)(((w >> 10) & 0x7800) | (w & 0x7FF)); in->vireads = vs; return;
    case 0x09: in->lop = L_ISUBIU; in->imm = (int32_t)(((w >> 10) & 0x7800) | (w & 0x7FF)); in->vireads = vs; return;
    case 0x10: in->lop = L_FCEQ; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x11: in->lop = L_FCSET; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x12: in->lop = L_FCAND; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x13: in->lop = L_FCOR; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x14: in->lop = L_FSEQ; in->imm = (int32_t)(((w >> 10) & 0x800) | (w & 0x7FF)); return;
    case 0x15: in->lop = L_FSSET; in->imm = (int32_t)(((w >> 10) & 0x800) | (w & 0x7FF)); return;
    case 0x16: in->lop = L_FSAND; in->imm = (int32_t)(((w >> 10) & 0x800) | (w & 0x7FF)); return;
    case 0x17: in->lop = L_FSOR; in->imm = (int32_t)(((w >> 10) & 0x800) | (w & 0x7FF)); return;
    case 0x18: in->lop = L_FMEQ; in->vireads = vs; return;
    case 0x1A: in->lop = L_FMAND; in->vireads = vs; return;
    case 0x1B: in->lop = L_FMOR; in->vireads = vs; return;
    case 0x1C: in->lop = L_FCGET; return;
    case 0x20: in->lop = L_B; return;
    case 0x21: in->lop = L_BAL; return;
    case 0x24: in->lop = L_JR; in->vireads = vs; return;
    case 0x25: in->lop = L_JALR; in->vireads = vs; return;
    case 0x28: in->lop = L_IBEQ; in->vireads = vs | vt; return;
    case 0x29: in->lop = L_IBNE; in->vireads = vs | vt; return;
    case 0x2C: in->lop = L_IBLTZ; in->vireads = vs; return;
    case 0x2D: in->lop = L_IBGTZ; in->vireads = vs; return;
    case 0x2E: in->lop = L_IBLEZ; in->vireads = vs; return;
    case 0x2F: in->lop = L_IBGEZ; in->vireads = vs; return;
    }
}

/* in reads a VF field w writes, or a VI register w's ILW loads */
static int reads_written(const VuInsn *in, const VuInsn *w) {
    int n;

    if (in->vireads & w->ilw_write) {
        return 1;
    }
    for (n = 0; n < in->nreads; n++) {
        const VfUse *use = &in->reads[n];

        if ((use->reg == w->uwreg && (use->mask & w->uwmask)) || (use->reg == w->lwreg && (use->mask & w->lwmask))) {
            return 1;
        }
    }
    return 0;
}

static void decode_all(void) {
    int n;

    for (n = 0; n < MICRO_INSNS; n++) {
        VuInsn *in = &code[n];
        uint32_t lo, hi;

        memcpy(&lo, micro + n * 8, 4);
        memcpy(&hi, micro + n * 8 + 4, 4);
        memset(in, 0, sizeof(*in));
        decode_upper(in, hi);
        if (hi & 0x80000000u) {
            in->lop = L_LOI;
            in->i_value = lo;
            in->lwrite = REG_NONE;
        } else {
            decode_lower(in, lo);
        }
        in->ebit = (hi >> 30) & 1;
        in->qwait = in->lop == L_DIV || in->lop == L_SQRT || in->lop == L_RSQRT || in->lop == L_WAITQ;
        in->pwait = IS_EFU(in->lop) || in->lop == L_WAITP;
        if ((in->lop >= L_FSAND && in->lop <= L_FMOR)) {
            vu.flags_read = 1;
        }
        if ((in->lop == L_ILW || in->lop == L_ILWR) && in->lt != 0 && in->lt < 16) {
            in->ilw_write = (uint16_t)(1u << in->lt);
        }
        in->uwreg = in->lwreg = REG_SCRATCH;
        if (in->uwrite != REG_NONE && in->uwrite != 0) {
            in->uwreg = in->uwrite;
            in->uwmask = in->udest;
        }
        if (in->lwrite != REG_NONE && in->lwrite != 0 && in->lwrite != in->uwrite) {
            in->lwreg = in->lwrite;
            in->lwmask = in->ldest;
        }
    }
    for (n = 0; n < MICRO_INSNS; n++) {
        int k;

        for (k = 1; k <= 3; k++) {
            if (reads_written(&code[n], &code[(n - k) & (MICRO_INSNS - 1)])) {
                code[n].hazard |= (uint8_t)(1 << (k - 1));
            }
        }
    }
    vu.decoded = 1;
}

/* The MAC flag's Z S U O, as the status flag has them */
static inline unsigned mac_zsuo(unsigned mac) {
    return ((mac & 0x000F) ? 1u : 0) | ((mac & 0x00F0) ? 2u : 0) | ((mac & 0x0F00) ? 4u : 0) |
           ((mac & 0xF000) ? 8u : 0);
}

/* ---- State ---------------------------------------------------------------------------------- */
void port_vu1_reset(void) {
    micro = ee_hw_ptr(0x11008000);
    data = ee_hw_ptr(0x1100C000);
    memset(&vu, 0, sizeof(vu));
    vu.vf[0][3] = 0x3F800000u;
    vu.next_due = UINT64_MAX;
}

void port_vu1_micro_written(void) {
    vu.decoded = 0;
}

uint8_t *port_vu1_micro(void) {
    return micro;
}

uint8_t *port_vu1_data(void) {
    return data;
}

static inline uint32_t *mem_qword(uint32_t address) {
    return (uint32_t *)(data + (address & (DATA_QWORDS - 1)) * 16);
}

static void set_q(uint32_t value, int latency, uint32_t status) {
    vu.q_next = value;
    vu.q_due = vu.cycle + (uint64_t)latency;
    vu.q_pending = 1;
    vu.q_status_next = status;
    if (vu.q_due < vu.next_due) {
        vu.next_due = vu.q_due;
    }
}

static void set_p(uint32_t value, int latency) {
    vu.p_next = value;
    vu.p_due = vu.cycle + (uint64_t)latency;
    vu.p_pending = 1;
    if (vu.p_due < vu.next_due) {
        vu.next_due = vu.p_due;
    }
}

/* At most two writes issue a cycle (an upper and a lower instruction), and each is due 4 cycles
   on, so no more than 8 are ever in flight. */
static inline void push_flags(int kind, uint16_t mac, uint32_t value) {
    FlagWrite *fw = &vu.flag_queue[(vu.flag_head + vu.flag_count++) & 15];

    fw->due = vu.cycle + 4;
    fw->kind = (uint8_t)kind;
    fw->mac = mac;
    fw->value = value;
    if (fw->due < vu.next_due) {
        vu.next_due = fw->due;
    }
}

static void settle(void) {
    if (vu.q_pending && vu.cycle >= vu.q_due) {
        vu.q = vu.q_next;
        vu.q_pending = 0;
        /* I and D, and their sticky bits */
        vu.status = (vu.status & ~0x30u) | vu.q_status_next | (vu.q_status_next << 6);
    }
    if (vu.p_pending && vu.cycle >= vu.p_due) {
        vu.p = vu.p_next;
        vu.p_pending = 0;
    }
    while (vu.flag_count && vu.flag_queue[vu.flag_head].due <= vu.cycle) {
        const FlagWrite *fw = &vu.flag_queue[vu.flag_head];

        vu.flag_head = (vu.flag_head + 1) & 15;
        vu.flag_count--;
        switch (fw->kind) {
        case FW_FMAC:
            /* Z S U O, and their sticky bits */
            vu.mac = fw->mac;
            vu.status = (vu.status & ~0xFu) | fw->value | (fw->value << 6);
            break;
        case FW_CLIP:
            vu.clip_visible = fw->value;
            break;
        default:
            vu.status = (vu.status & 0x3Fu) | fw->value;
            break;
        }
    }
    vu.next_due = vu.flag_count ? vu.flag_queue[vu.flag_head].due : UINT64_MAX;
    if (vu.q_pending && vu.q_due < vu.next_due) {
        vu.next_due = vu.q_due;
    }
    if (vu.p_pending && vu.p_due < vu.next_due) {
        vu.next_due = vu.p_due;
    }
}

/* Lets everything in flight finish, as the pipelines drain after a program ends. */
static void drain(void) {
    vu.cycle += 64;
    settle();
}

/* x*x + y*y + z*z, as the EFU's ESADD */
static uint32_t efu_sum_squares(const uint32_t *v) {
    return vf_add(vf_add(vf_mul(v[0], v[0]), vf_mul(v[1], v[1])), vf_mul(v[2], v[2]));
}

/* EATAN's series in t = (x - 1)/(x + 1) (EATANxy's in (y - x)/(y + x)): pi/4 + t*c1 + t^3*c2 ...
   with the constants of the VU1 manual */
static uint32_t efu_atan(uint32_t t) {
    static const float c[8] = {0.999999344348907f, -0.333298563957214f, 0.199465364217758f,
                               -0.139085337519646f, 0.096420042216778f, -0.055909886956215f,
                               0.021861229091883f, -0.004054057877511f};
    uint32_t sum = vf_mul(t, f32_bits(c[0])), power = t;
    int n;

    for (n = 1; n < 8; n++) {
        power = vf_mul(vf_mul(power, t), t);
        sum = vf_add(sum, vf_mul(power, f32_bits(c[n])));
    }
    return vf_add(sum, f32_bits(0.785398185253143f));
}

/* ESIN: x + x^3*s2 + x^5*s3 + x^7*s4 + x^9*s5 */
static uint32_t efu_sin(uint32_t x) {
    static const float c[4] = {-0.166666567325592f, 0.008333025500178f, -0.000198074136279f,
                               0.000002601886990f};
    uint32_t square = vf_mul(x, x), power = x, sum = x;
    int n;

    for (n = 0; n < 4; n++) {
        power = vf_mul(power, square);
        sum = vf_add(sum, vf_mul(power, f32_bits(c[n])));
    }
    return sum;
}

/* EEXP: e^-x as 1 / (1 + x*e1 + x^2*e2 ... + x^6*e6)^4 */
static uint32_t efu_exp(uint32_t x) {
    static const float c[6] = {0.249998688697815f, 0.031257584691048f, 0.002591371303424f,
                               0.000171562001924f, 0.000005430199963f, 0.000000690600018f};
    uint32_t sum = f32_bits(1.0f), power = f32_bits(1.0f);
    int n;

    for (n = 0; n < 6; n++) {
        power = vf_mul(power, x);
        sum = vf_add(sum, vf_mul(power, f32_bits(c[n])));
    }
    sum = vf_mul(sum, sum);
    sum = vf_mul(sum, sum);
    return vf_div(f32_bits(1.0f), sum);
}

static uint32_t efu_result(const VuInsn *in) {
    const uint32_t *v = vu.vf[in->ls];
    uint32_t one = f32_bits(1.0f), x = v[in->fsf];

    switch (in->lop) {
    case L_ESADD: return efu_sum_squares(v);
    case L_ERSADD: return vf_div(one, efu_sum_squares(v));
    case L_ELENG: return vf_sqrt(efu_sum_squares(v));
    case L_ERLENG: return vf_div(one, vf_sqrt(efu_sum_squares(v)));
    case L_EATANXY: return efu_atan(vf_div(vf_sub(v[1], v[0]), vf_add(v[1], v[0])));
    case L_EATANXZ: return efu_atan(vf_div(vf_sub(v[2], v[0]), vf_add(v[2], v[0])));
    case L_ESUM: return vf_add(vf_add(vf_add(v[0], v[1]), v[2]), v[3]);
    case L_ESQRT: return vf_sqrt(x);
    case L_ERSQRT: return vf_div(one, vf_sqrt(x));
    case L_ERCPR: return vf_div(one, x);
    case L_ESIN: return efu_sin(x);
    case L_EATAN: return efu_atan(vf_div(vf_sub(x, one), vf_add(x, one)));
    default: return efu_exp(x);
    }
}

/* The value of a VI register a conditional branch reads (see the top); stalled: it waited for an
   ILW. An instruction writes one VI register at most, so the 4 cycles before the branch hold 4
   writes at most. */
static uint16_t branch_vi(int reg, int stalled) {
    const ViWrite *h = &vu.vi_writes[(vu.vi_write_count - 1) & 7];
    uint16_t value = vu.vi[reg];
    unsigned n;

    if (reg == 0 || stalled || h->reg != reg || h->flag_read || h->cycle + 1 != vu.cycle) {
        return value;
    }
    for (n = 1; n <= 4; n++) {
        h = &vu.vi_writes[(vu.vi_write_count - n) & 7];
        if (h->cycle + 4 < vu.cycle) {
            break;
        }
        if (h->reg == reg) {
            if (h->flag_read) {
                break;
            }
            value = h->old;
        }
    }
    return value;
}

/* ---- Running --------------------------------------------------------------------------------- */
/* An upper instruction of the arithmetic kind; each pair of arith and bsrc is its own copy, so the
   two are one jump. */
static inline __attribute__((always_inline)) V4 upper_arith(const VuInsn *in, int arith, int bsrc,
                                                             unsigned *mac) {
    V4 a = v4_load(vu.vf[in->fs]), b, lanes = field_lanes[in->udest];

    switch (bsrc) {
    case B_FT: b = v4_load(vu.vf[in->ft]); break;
    case B_BC: b = v4_splat(vu.vf[in->ft][in->bc]); break;
    case B_Q: b = v4_splat(vu.q); break;
    default: b = v4_splat(vu.i); break;
    }
    switch (arith) {
    case A_ADD: return v4_op(A_ADD, a, b, lanes, mac);
    case A_SUB: return v4_op(A_SUB, a, b, lanes, mac);
    case A_MUL: return v4_op(A_MUL, a, b, lanes, mac);
    case A_MADD: return v4_op(A_ADD, v4_load(vu.vf[REG_ACC]), v4_op(A_MUL, a, b, lanes, NULL), lanes, mac);
    case A_MSUB: return v4_op(A_SUB, v4_load(vu.vf[REG_ACC]), v4_op(A_MUL, a, b, lanes, NULL), lanes, mac);
    case A_MAX: return v4_max(a, b);
    default: return v4_mini(a, b);
    }
}

/* the lower (0) or upper (1) word of the instruction pair at pc, for messages */
static uint32_t micro_word(uint32_t pc, int upper) {
    uint32_t w;

    memcpy(&w, micro + (pc & (MICRO_INSNS - 1)) * 8 + upper * 4, 4);
    return w;
}

static void run(uint32_t pc, uint32_t top, uint32_t itop) {
    int branch_pending = 0;
    uint32_t branch_target = 0;
    int end_next = 0;
    int guard = 0;
    int saved_round = fegetround();
#if VU_DAZ
    unsigned csr; /* MXCSR rounding toward zero */
#endif
    uint64_t start = 0, gif_before = 0;
    /* When no instruction in micro memory reads the MAC or status flag (FSxxx, FMxxx) or sets the
       sticky bits (FSSET), the flag writes are not queued: nothing can see them while the program
       runs. What a later program could see is what is left once they have all landed (drain), so
       only that is kept: the last FMAC instruction's result and Z U O, and the Z S U O of them all
       for the sticky bits. */
    int flags_read;
    uint64_t cycle; /* vu.cycle, kept in a register: the helpers read vu.cycle, set before each */
    /* the last three instructions run (the newest first), and the cycles they issued */
    const VuInsn *last[3] = {NULL, NULL, NULL};
    uint64_t issued[3] = {0, 0, 0};
    uint32_t last_pc = pc; /* not the one before pc: nothing ran before it */
    int in_line = 0; /* how many of them ran in a line before this one, up to 3 */
    int fmac_seen = 0;
    V4 last_value = {0, 0, 0, 0}, signs_seen = {0, 0, 0, 0};
    unsigned last_dest = 0, last_zuo = 0, zuo_seen = 0;

    if (port_options.stats) {
        start = port_ticks();
        gif_before = port_stats.gif_ticks;
    }
    if (!vu.decoded) {
        vu.flags_read = 0;
        decode_all();
    }
    flags_read = vu.flags_read;
    memset(vu.vi_writes, 0, sizeof(vu.vi_writes));
    fesetround(FE_TOWARDZERO);
#if VU_DAZ
    csr = _mm_getcsr();
    _mm_setcsr(csr | MXCSR_DAZ);
#endif
    cycle = vu.cycle;
    for (;;) {
        const VuInsn *in = &code[pc & (MICRO_INSNS - 1)];
        uint64_t need = cycle;
        V4 uval = {0, 0, 0, 0};
        V4 lval = {0, 0, 0, 0};
        unsigned mac = 0; /* the Z, U and O bits of the MAC flag the upper sets */
        int n, f;
        int branch_now = 0;
        int stalled;
        uint32_t target = 0;

        /* stalls. A VF write, and an ILW's, is due 4 cycles after it issues, and an instruction
           issues a cycle or more after the one before, so only the last three instructions' writes
           can still be due. When they are the three before this one in memory, decode has said
           which of them it reads from (hazard); after a branch they are looked at. */
        in_line = (pc & (MICRO_INSNS - 1)) == ((last_pc + 1) & (MICRO_INSNS - 1)) ? (in_line < 3 ? in_line + 1 : 3) : 0;
        if (in_line == 3) {
            if (in->hazard) {
                for (n = 0; n < 3; n++) {
                    if ((in->hazard & (1 << n)) && issued[n] + 4 > need) {
                        need = issued[n] + 4;
                    }
                }
            }
        } else {
            for (n = 0; n < 3; n++) {
                if (last[n] && issued[n] + 4 > need && reads_written(in, last[n])) {
                    need = issued[n] + 4;
                }
            }
        }
        if (in->qwait && vu.q_pending && vu.q_due > need) {
            need = vu.q_due;
        }
        if (in->pwait && vu.p_pending && vu.p_due - 1 > need) {
            need = vu.p_due - 1;
        }
        stalled = need > cycle;
        cycle = need;
        vu.cycle = cycle; /* for the helpers below */
        if (cycle >= vu.next_due) {
            settle();
        }
        if (in->pwait && vu.p_pending && cycle + 1 >= vu.p_due) {
            /* the stall ends a cycle before P is due, with P written */
            vu.p = vu.p_next;
            vu.p_pending = 0;
        }

        /* upper, into uval (and the Z, U and O flags it sets into mac) */
        switch (in->uop) {
        case U_NOP:
            break;
#define ARITH(arith_)                                                                              \
    case U_ARITH + (arith_) * 4 + B_FT: uval = upper_arith(in, arith_, B_FT, &mac); break;        \
    case U_ARITH + (arith_) * 4 + B_BC: uval = upper_arith(in, arith_, B_BC, &mac); break;        \
    case U_ARITH + (arith_) * 4 + B_Q: uval = upper_arith(in, arith_, B_Q, &mac); break;          \
    case U_ARITH + (arith_) * 4 + B_I: uval = upper_arith(in, arith_, B_I, &mac); break
        ARITH(A_ADD);
        ARITH(A_SUB);
        ARITH(A_MUL);
        ARITH(A_MADD);
        ARITH(A_MSUB);
        ARITH(A_MAX);
        ARITH(A_MINI);
#undef ARITH
        case U_ITOF:
            uval = v4_itof(v4_load(vu.vf[in->fs]), fixed_shift[in->bc]);
            break;
        case U_FTOI:
            uval = v4_ftoi(v4_load(vu.vf[in->fs]), fixed_shift[in->bc]);
            break;
        case U_ABS:
            for (f = 0; f < 4; f++) {
                uval[f] = vu.vf[in->fs][f] & 0x7FFFFFFFu;
            }
            break;
        case U_CLIP: {
            /* compared as sign and magnitude; a denormal w is the largest denormal */
            uint32_t w = vu.vf[in->ft][3] & 0x7FFFFFFFu;
            uint32_t judge = 0;

            if ((w & 0x7F800000u) == 0) {
                w = 0x007FFFFFu;
            }
            for (f = 0; f < 3; f++) {
                uint32_t v = vu.vf[in->fs][f];

                if ((v & 0x7FFFFFFFu) > w) {
                    judge |= (v & SIGN ? 2u : 1u) << (f * 2);
                }
            }
            vu.clip = ((vu.clip << 6) | judge) & 0xFFFFFFu;
            push_flags(FW_CLIP, 0, vu.clip);
            break;
        }
        case U_OPMULA:
        case U_OPMSUB: {
            const uint32_t *s = vu.vf[in->fs], *t = vu.vf[in->ft];
            static const int i1[3] = {1, 2, 0}, i2[3] = {2, 0, 1};
            for (f = 0; f < 3; f++) {
                unsigned fl = 0;

                if (in->uop == U_OPMULA) {
                    uval[f] = vf_op(A_MUL, s[i1[f]], t[i2[f]], &fl);
                } else {
                    uval[f] = vf_op(A_SUB, vu.vf[REG_ACC][f], vf_mul(s[i1[f]], t[i2[f]]), &fl);
                }
                mac |= fl << (3 - f);
            }
            break;
        }
        default:
            PORT_FATAL("VU1: unknown upper instruction %08X at 0x%03X", micro_word(pc, 1), pc);
        }
        if (in->uflags && !flags_read) {
            fmac_seen = 1;
            last_value = uval;
            last_dest = in->udest;
            last_zuo = mac;
            zuo_seen |= mac;
            signs_seen |= uval & field_lanes[in->udest];
        } else if (in->uflags) {
            V4 sign = (uval >> 31) & field_lanes[in->udest];

            mac |= (sign[0] << 7) | (sign[1] << 6) | (sign[2] << 5) | (sign[3] << 4); /* S */
            /* the same flags again change nothing (unless an FSSET came between) */
            if ((int32_t)mac != vu.mac_queued) {
                vu.mac_queued = (int32_t)mac;
                push_flags(FW_FMAC, (uint16_t)mac, mac_zsuo(mac));
            }
        }

        /* lower */
#define SET_VI(r_, value_, flag_read_)                                                             \
    do {                                                                                           \
        if (r_) {                                                                                  \
            ViWrite *w_ = &vu.vi_writes[vu.vi_write_count++ & 7];                                  \
            w_->cycle = cycle;                                                                     \
            w_->reg = (uint8_t)(r_);                                                               \
            w_->flag_read = (flag_read_);                                                          \
            w_->old = vu.vi[r_];                                                                   \
            vu.vi[r_] = (uint16_t)(value_);                                                        \
        }                                                                                          \
    } while (0)
        switch (in->lop) {
        case L_NOP:
            break;
        case L_LOI:
            break; /* after the upper has read I */
        case L_LQ:
        case L_LQI:
        case L_LQD: {
            uint32_t address;

            if (in->lop == L_LQ) {
                address = (uint32_t)(vu.vi[in->ls] + in->imm);
            } else if (in->lop == L_LQD) {
                SET_VI(in->ls, vu.vi[in->ls] - 1, 0);
                address = vu.vi[in->ls];
            } else {
                address = vu.vi[in->ls];
            }
            lval = v4_load(mem_qword(address));
            if (in->lop == L_LQI) {
                SET_VI(in->ls, vu.vi[in->ls] + 1, 0);
            }
            break;
        }
        case L_SQ:
        case L_SQI:
        case L_SQD: {
            uint32_t address;
            uint32_t *q;

            if (in->lop == L_SQ) {
                address = (uint32_t)(vu.vi[in->lt] + in->imm);
            } else if (in->lop == L_SQD) {
                SET_VI(in->lt, vu.vi[in->lt] - 1, 0);
                address = vu.vi[in->lt];
            } else {
                address = vu.vi[in->lt];
            }
            q = mem_qword(address);
            for (f = 0; f < 4; f++) {
                if (in->ldest & (8 >> f)) {
                    q[f] = vu.vf[in->ls][f];
                }
            }
            if (in->lop == L_SQI) {
                SET_VI(in->lt, vu.vi[in->lt] + 1, 0);
            }
            break;
        }
        case L_ILW:
        case L_ILWR: {
            uint32_t *q = mem_qword((uint32_t)(vu.vi[in->ls] + (in->lop == L_ILW ? in->imm : 0)));
            for (f = 0; f < 4; f++) {
                if (in->ldest & (8 >> f)) {
                    SET_VI(in->lt, q[f], 0);
                    break;
                }
            }
            break;
        }
        case L_ISW:
        case L_ISWR: {
            uint32_t *q = mem_qword((uint32_t)(vu.vi[in->ls] + (in->lop == L_ISW ? in->imm : 0)));
            for (f = 0; f < 4; f++) {
                if (in->ldest & (8 >> f)) {
                    q[f] = vu.vi[in->lt];
                }
            }
            break;
        }
        case L_IADDIU: SET_VI(in->lt, vu.vi[in->ls] + in->imm, 0); break;
        case L_ISUBIU: SET_VI(in->lt, vu.vi[in->ls] - in->imm, 0); break;
        case L_IADDI: SET_VI(in->lt, vu.vi[in->ls] + in->imm, 0); break;
        case L_IADD: SET_VI(in->ld, vu.vi[in->ls] + vu.vi[in->lt], 0); break;
        case L_ISUB: SET_VI(in->ld, vu.vi[in->ls] - vu.vi[in->lt], 0); break;
        case L_IAND: SET_VI(in->ld, vu.vi[in->ls] & vu.vi[in->lt], 0); break;
        case L_IOR: SET_VI(in->ld, vu.vi[in->ls] | vu.vi[in->lt], 0); break;
        case L_MOVE:
            lval = v4_load(vu.vf[in->ls]);
            break;
        case L_MR32:
            lval[0] = vu.vf[in->ls][1];
            lval[1] = vu.vf[in->ls][2];
            lval[2] = vu.vf[in->ls][3];
            lval[3] = vu.vf[in->ls][0];
            break;
        case L_MTIR: SET_VI(in->lt, vu.vf[in->ls][in->fsf], 0); break;
        case L_MFIR:
            for (f = 0; f < 4; f++) {
                lval[f] = (uint32_t)(int32_t)(int16_t)vu.vi[in->ls];
            }
            break;
        case L_DIV: {
            uint32_t s = vu.vf[in->ls][in->fsf], t = vu.vf[in->lt][in->ftf];
            uint32_t status = !vf_is_zero(t) ? 0 : vf_is_zero(s) ? 0x10u : 0x20u;

            set_q(vf_div(s, t), 7, status);
            break;
        }
        case L_SQRT: {
            uint32_t t = vu.vf[in->lt][in->ftf];

            set_q(vf_sqrt(t), 7, (t & SIGN) && !vf_is_zero(t) ? 0x10u : 0);
            break;
        }
        case L_RSQRT: {
            uint32_t s = vu.vf[in->ls][in->fsf], t = vu.vf[in->lt][in->ftf];

            if (vf_is_zero(t)) {
                /* as DIV: D, or I for 0/0, and the largest float with s's sign */
                set_q((s & SIGN) | VU_MAX, 13, vf_is_zero(s) ? 0x10u : 0x20u);
            } else {
                set_q(vf_div(s, vf_sqrt(t)), 13, t & SIGN ? 0x10u : 0);
            }
            break;
        }
        case L_WAITQ:
        case L_WAITP:
            break;
        case L_FCAND: SET_VI(1, (vu.clip_visible & (uint32_t)in->imm) != 0, 1); break;
        case L_FCEQ: SET_VI(1, vu.clip_visible == (uint32_t)in->imm, 1); break;
        case L_FCOR: SET_VI(1, ((vu.clip_visible | (uint32_t)in->imm) & 0xFFFFFFu) == 0xFFFFFFu, 1); break;
        case L_FCGET: SET_VI(in->lt, vu.clip_visible & 0xFFF, 1); break;
        case L_FCSET:
            vu.clip = (uint32_t)in->imm;
            push_flags(FW_CLIP, 0, vu.clip);
            break;
        case L_FSAND: SET_VI(in->lt, vu.status & (uint32_t)in->imm, 1); break;
        case L_FSEQ: SET_VI(in->lt, vu.status == (uint32_t)in->imm, 1); break;
        case L_FSOR: SET_VI(in->lt, vu.status | (uint32_t)in->imm, 1); break;
        case L_FSSET:
            push_flags(FW_STICKY, 0, (uint32_t)in->imm & 0xFC0u);
            vu.mac_queued = -1;
            break;
        case L_FMAND: SET_VI(in->lt, vu.mac & vu.vi[in->ls], 1); break;
        case L_FMEQ: SET_VI(in->lt, vu.mac == vu.vi[in->ls], 1); break;
        case L_FMOR: SET_VI(in->lt, vu.mac | vu.vi[in->ls], 1); break;
        case L_B:
        case L_BAL:
            branch_now = 1;
            target = pc + 1 + (uint32_t)in->imm;
            if (in->lop == L_BAL) {
                /* in a branch's delay slot, the link is the instruction after the first target */
                SET_VI(in->lt, branch_pending ? branch_target + 1 : pc + 2, 0);
            }
            break;
        case L_JR:
        case L_JALR:
            branch_now = 1;
            target = vu.vi[in->ls];
            if (in->lop == L_JALR) {
                SET_VI(in->lt, branch_pending ? branch_target + 1 : pc + 2, 0);
            }
            break;
        case L_IBEQ:
        case L_IBNE:
        case L_IBLTZ:
        case L_IBGTZ:
        case L_IBLEZ:
        case L_IBGEZ: {
            uint16_t vs = branch_vi(in->ls, stalled);
            uint16_t vt = branch_vi(in->lt, stalled);
            int16_t s = (int16_t)vs;
            int taken;

            switch (in->lop) {
            case L_IBEQ: taken = vt == vs; break;
            case L_IBNE: taken = vt != vs; break;
            case L_IBLTZ: taken = s < 0; break;
            case L_IBGTZ: taken = s > 0; break;
            case L_IBLEZ: taken = s <= 0; break;
            default: taken = s >= 0; break;
            }
            if (taken) {
                branch_now = 1;
                target = pc + 1 + (uint32_t)in->imm;
            }
            break;
        }
        case L_XTOP: SET_VI(in->lt, top, 0); break;
        case L_XITOP: SET_VI(in->lt, itop, 0); break;
        case L_XGKICK:
#if VU_DAZ
            _mm_setcsr(csr); /* the GS as before: rounding toward zero, without DAZ */
#endif
            port_gif_path1_kick(vu.vi[in->ls]);
#if VU_DAZ
            _mm_setcsr(csr | MXCSR_DAZ);
#endif
            break;
        case L_MFP:
            for (f = 0; f < 4; f++) {
                lval[f] = vu.p;
            }
            break;
        case L_ESADD:
        case L_ERSADD:
        case L_ELENG:
        case L_ERLENG:
        case L_EATANXY:
        case L_EATANXZ:
        case L_ESUM:
        case L_ESQRT:
        case L_ERSQRT:
        case L_ERCPR:
        case L_ESIN:
        case L_EATAN:
        case L_EEXP:
            set_p(efu_result(in), efu_latency[in->lop - L_ESADD]);
            break;
        case L_RINIT: vu.r = 0x3F800000u | (vu.vf[in->ls][in->fsf] & 0x007FFFFFu); break;
        case L_RXOR: vu.r = 0x3F800000u | ((vu.r ^ vu.vf[in->ls][in->fsf]) & 0x007FFFFFu); break;
        case L_RGET:
        case L_RNEXT:
            if (in->lop == L_RNEXT) {
                uint32_t x = vu.r & 0x007FFFFFu;
                int bit = ((x >> 4) ^ (x >> 22)) & 1;

                vu.r = 0x3F800000u | (((x << 1) | (uint32_t)bit) & 0x007FFFFFu);
            }
            for (f = 0; f < 4; f++) {
                lval[f] = vu.r;
            }
            break;
        default:
            PORT_FATAL("VU1: unknown lower instruction %08X at 0x%03X", micro_word(pc, 0), pc);
        }
#undef SET_VI

        /* write back: the upper first, then the lower (decode dropped it if it is the same register) */
        if (in->uwmask) {
            V4 lanes = field_lanes[in->uwmask];

            v4_store(vu.vf[in->uwreg], (v4_load(vu.vf[in->uwreg]) & ~lanes) | (uval & lanes));
        }
        if (in->lwmask) {
            V4 lanes = field_lanes[in->lwmask];

            v4_store(vu.vf[in->lwreg], (v4_load(vu.vf[in->lwreg]) & ~lanes) | (lval & lanes));
        }
        last[2] = last[1];
        last[1] = last[0];
        last[0] = in;
        issued[2] = issued[1];
        issued[1] = issued[0];
        issued[0] = cycle;
        last_pc = pc;
        if (in->lop == L_LOI) {
            vu.i = in->i_value;
        }
        cycle++;

        /* where next */
        if (end_next) {
            vu.pc = (pc + 1) & (MICRO_INSNS - 1);
            break;
        }
        if (in->ebit) {
            end_next = 1;
        }
        if (branch_pending) {
            pc = branch_target & (MICRO_INSNS - 1);
            branch_pending = 0;
        } else {
            pc = (pc + 1) & (MICRO_INSNS - 1);
        }
        if (branch_now) {
            branch_pending = 1;
            branch_target = target;
        }
        if (++guard > 50000000) {
            PORT_FATAL("VU1 ran 50 million instructions without ending (at 0x%03X)", pc);
        }
    }
    vu.cycle = cycle;
    drain();
    if (fmac_seen) {
        /* the flag writes not queued, landed: the last one's flags, and the sticky bits of all */
        V4 sign = (last_value >> 31) & field_lanes[last_dest];
        unsigned mac = last_zuo | (sign[0] << 7) | (sign[1] << 6) | (sign[2] << 5) | (sign[3] << 4);
        unsigned seen = mac_zsuo(zuo_seen) | (v4_any(signs_seen & SIGN) ? 2u : 0);

        vu.mac = mac;
        vu.mac_queued = (int32_t)mac;
        vu.status = (vu.status & ~0xFu) | mac_zsuo(mac) | (seen << 6);
    }
#if VU_DAZ
    _mm_setcsr(csr);
#endif
    fesetround(saved_round);
    if (port_options.stats) {
        /* the GIF hand-offs of its XGKICKs (and the PATH3 they let go on) are counted there */
        port_stats.vu1_ticks += port_ticks() - start - (port_stats.gif_ticks - gif_before);
        port_stats.vu1_runs++;
        port_stats.vu1_insns += (uint64_t)guard + 1;
        port_stats.vu1_cycles = vu.cycle;
    }
}

void port_vu1_start(uint32_t address, uint32_t top, uint32_t itop) {
    run(address, top, itop);
}

void port_vu1_continue(uint32_t top, uint32_t itop) {
    run(vu.pc, top, itop);
}
