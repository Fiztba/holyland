/* VU1 on the CPU: an interpreter for the microprograms the game uploads (game/data/vudata.c; see
   tools/vu1_disasm.py for their text).

   The game's microprograms are software-pipelined: they issue an instruction and use its result a
   fixed number of cycles later, and in places they rely on reading the old value before a result is
   due. So this counts cycles as VU1 does (one instruction pair a cycle, plus the stalls the hardware
   inserts) and makes each result visible when VU1 would:
   - VF registers: an FMAC result (upper instructions, and LQ, MOVE, MR32, MFIR, MFP) is due 4 cycles
     after issue. A read before then stalls until it is due, as on VU1, so the value read is always
     the new one; only the cycle count moves.
   - Q: DIV and SQRT are due 7 cycles after issue, RSQRT 13. Until then Q keeps its old value (there
     is no stall), WAITQ waits for it, and a second divide waits for the first.
   - P: the EFU result is due after the instruction's latency (ESADD 11); MFP reads the old P until
     then.
   - The clip flag: each CLIP shifts the flag at once, but FCAND/FCEQ/FCOR/FCGET see a CLIP's result
     only 4 cycles after it was issued.
   Within a pair the upper and the lower instruction both read their operands before either writes;
   when both write the same VF register the lower one's write is dropped. The I register an upper
   instruction reads is the one before its own pair's LOI.

   Floats follow VU1 where it matters for these programs: no infinities or NaNs (an exponent of 255 is
   read as the largest float), denormals read and write as zero, and arithmetic rounds toward zero.

   VI writes are immediate, with one exception: a conditional branch reads the VI register an
   integer op in the instruction just before it wrote as it was before that op. Of the game's
   programs only the one at 0x700 relies on it (its loop at 0x7BE).

   Only the DMA controller's thread calls in here (port/platform/dmac.c). */
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"

#define MICRO_INSNS 2048 /* 16 KB of micro memory */
#define DATA_QWORDS 1024 /* 16 KB of data memory */
#define REG_ACC 32       /* VF write target: the accumulator */
#define REG_NONE 0xFF

enum {
    /* upper: the generic arithmetic ones (op, second operand, destination) */
    U_NOP, U_ARITH, U_ITOF, U_FTOI, U_ABS, U_CLIP, U_OPMULA, U_OPMSUB,
};
enum { A_ADD, A_SUB, A_MUL, A_MADD, A_MSUB, A_MAX, A_MINI };
enum { B_FT, B_BC, B_Q, B_I };

enum {
    L_NOP, L_LOI, L_LQ, L_SQ, L_LQI, L_LQD, L_SQI, L_SQD, L_ILW, L_ISW, L_ILWR, L_ISWR,
    L_IADDIU, L_ISUBIU, L_IADD, L_ISUB, L_IADDI, L_IAND, L_IOR,
    L_MOVE, L_MR32, L_MTIR, L_MFIR, L_DIV, L_SQRT, L_RSQRT, L_WAITQ,
    L_FCAND, L_FCSET, L_FCEQ, L_FCOR, L_FCGET,
    L_B, L_BAL, L_JR, L_JALR, L_IBEQ, L_IBNE, L_IBLTZ, L_IBGTZ, L_IBLEZ, L_IBGEZ,
    L_XTOP, L_XITOP, L_XGKICK, L_MFP, L_WAITP, L_ESADD, L_ERSADD, L_ELENG, L_ERLENG, L_ESQRT,
    L_ERSQRT, L_ERCPR, L_ESUM, L_RINIT, L_RGET, L_RNEXT, L_RXOR,
    L_UNKNOWN,
};

typedef struct {
    uint8_t reg;  /* VF register */
    uint8_t mask; /* fields (x = 8 .. w = 1) */
} VfUse;

typedef struct {
    /* upper */
    uint8_t uop, arith, bsrc, bc, udest, fd, fs, ft;
    uint8_t uwrite; /* VF register written (REG_ACC, REG_NONE) */
    /* lower */
    uint8_t lop, ldest, lt, ls, ld, fsf, ftf;
    uint8_t lwrite; /* VF register written (REG_NONE) */
    int32_t imm;
    uint32_t i_value; /* LOI */
    uint8_t ebit;
    uint8_t nreads;
    VfUse reads[4];
} VuInsn;

typedef struct {
    uint32_t value;
    uint64_t due;
} ClipWrite;

static struct {
    uint32_t vf[32][4];
    uint16_t vi[16];
    uint32_t acc[4];
    uint32_t q, p, i, r;
    uint32_t q_next, p_next;
    uint64_t q_due, p_due;
    int q_pending, p_pending;
    uint32_t clip;         /* every CLIP so far */
    uint32_t clip_visible; /* what the flag instructions see */
    ClipWrite clip_queue[8];
    int clip_count;
    uint64_t ready[32][4]; /* the cycle each VF field's last write is due */
    uint64_t cycle;
    uint32_t pc;           /* where MSCNT continues */
    int decoded;           /* micro memory decoded since the last upload */
} vu;

static uint8_t *micro;    /* 0x11008000 */
static uint8_t *data;     /* 0x1100C000 */
static VuInsn code[MICRO_INSNS];

/* ---- Floats --------------------------------------------------------------------------------- */
static inline float as_float(uint32_t bits) {
    float f;

    if ((bits & 0x7F800000u) == 0) {
        bits &= 0x80000000u;
    } else if ((bits & 0x7F800000u) == 0x7F800000u) {
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    }
    memcpy(&f, &bits, 4);
    return f;
}

static inline uint32_t as_bits(float f) {
    uint32_t bits;

    memcpy(&bits, &f, 4);
    if ((bits & 0x7F800000u) == 0) {
        bits &= 0x80000000u;
    } else if ((bits & 0x7F800000u) == 0x7F800000u) {
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    }
    return bits;
}

/* a < b for VU floats compared as sign and magnitude */
static inline int vu_less(uint32_t a, uint32_t b) {
    int32_t ka = (a & 0x80000000u) ? -(int32_t)(a & 0x7FFFFFFFu) : (int32_t)a;
    int32_t kb = (b & 0x80000000u) ? -(int32_t)(b & 0x7FFFFFFFu) : (int32_t)b;

    return ka < kb;
}

static uint32_t ftoi(uint32_t bits, float scale) {
    float f = as_float(bits) * scale;

    if (f >= 2147483648.0f) {
        return 0x7FFFFFFFu;
    }
    if (f <= -2147483648.0f) {
        return 0x80000000u;
    }
    return (uint32_t)(int32_t)f;
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
            add_read(in, fs, 0xE);
            add_read(in, ft, 0xE);
            in->uwrite = REG_ACC;
            in->udest = 0xE;
            return;
        } else if (sop == 0x2F) {
            in->uop = U_NOP;
            return;
        } else if (sop >= 0x1C && sop < 0x2E) {
            static const struct { uint8_t arith, bsrc; } acc_qi[18] = {
                {A_MUL, B_Q}, {0, 0}, {A_MUL, B_I}, {0, 0}, {A_ADD, B_Q}, {A_MADD, B_Q},
                {A_ADD, B_I}, {A_MADD, B_I}, {A_SUB, B_Q}, {A_MSUB, B_Q}, {A_SUB, B_I}, {A_MSUB, B_I},
                {A_ADD, B_FT}, {A_MADD, B_FT}, {A_MUL, B_FT}, {0, 0}, {A_SUB, B_FT}, {A_MSUB, B_FT}};

            in->arith = acc_qi[sop - 0x1C].arith;
            in->bsrc = acc_qi[sop - 0x1C].bsrc;
            in->uwrite = REG_ACC;
        } else {
            in->uop = U_NOP;
            return;
        }
    } else {
        in->uop = U_NOP;
        return;
    }
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
        case 0x30: in->lop = L_IADD; return;
        case 0x31: in->lop = L_ISUB; return;
        case 0x32: in->lop = L_IADDI; in->imm = sext(id, 5); return;
        case 0x34: in->lop = L_IAND; return;
        case 0x35: in->lop = L_IOR; return;
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
        case 0x34: in->lop = L_LQI; in->lwrite = (uint8_t)it; return;
        case 0x35: in->lop = L_SQI; add_read(in, is, dest); return;
        case 0x36: in->lop = L_LQD; in->lwrite = (uint8_t)it; return;
        case 0x37: in->lop = L_SQD; add_read(in, is, dest); return;
        case 0x38: in->lop = L_DIV; add_read(in, is, 8 >> in->fsf); add_read(in, it, 8 >> in->ftf); return;
        case 0x39: in->lop = L_SQRT; add_read(in, it, 8 >> in->ftf); return;
        case 0x3A: in->lop = L_RSQRT; add_read(in, is, 8 >> in->fsf); add_read(in, it, 8 >> in->ftf); return;
        case 0x3B: in->lop = L_WAITQ; return;
        case 0x3C: in->lop = L_MTIR; add_read(in, is, 8 >> in->fsf); return;
        case 0x3D: in->lop = L_MFIR; in->lwrite = (uint8_t)it; return;
        case 0x3E: in->lop = L_ILWR; return;
        case 0x3F: in->lop = L_ISWR; return;
        case 0x40: in->lop = L_RNEXT; in->lwrite = (uint8_t)it; return;
        case 0x41: in->lop = L_RGET; in->lwrite = (uint8_t)it; return;
        case 0x42: in->lop = L_RINIT; add_read(in, is, 8 >> in->fsf); return;
        case 0x43: in->lop = L_RXOR; add_read(in, is, 8 >> in->fsf); return;
        case 0x64: in->lop = L_MFP; in->lwrite = (uint8_t)it; return;
        case 0x68: in->lop = L_XTOP; return;
        case 0x69: in->lop = L_XITOP; return;
        case 0x6C: in->lop = L_XGKICK; return;
        case 0x70: in->lop = L_ESADD; add_read(in, is, 0xE); return;
        case 0x71: in->lop = L_ERSADD; add_read(in, is, 0xE); return;
        case 0x72: in->lop = L_ELENG; add_read(in, is, 0xE); return;
        case 0x73: in->lop = L_ERLENG; add_read(in, is, 0xE); return;
        case 0x76: in->lop = L_ESUM; add_read(in, is, 0xF); return;
        case 0x78: in->lop = L_ESQRT; add_read(in, is, 8 >> in->fsf); return;
        case 0x79: in->lop = L_ERSQRT; add_read(in, is, 8 >> in->fsf); return;
        case 0x7A: in->lop = L_ERCPR; add_read(in, is, 8 >> in->fsf); return;
        case 0x7B: in->lop = L_WAITP; return;
        }
        return;
    }
    switch (top) {
    case 0x00: in->lop = L_LQ; in->lwrite = (uint8_t)it; return;
    case 0x01: in->lop = L_SQ; add_read(in, is, dest); return;
    case 0x04: in->lop = L_ILW; return;
    case 0x05: in->lop = L_ISW; return;
    case 0x08: in->lop = L_IADDIU; in->imm = (int32_t)(((w >> 10) & 0x7800) | (w & 0x7FF)); return;
    case 0x09: in->lop = L_ISUBIU; in->imm = (int32_t)(((w >> 10) & 0x7800) | (w & 0x7FF)); return;
    case 0x10: in->lop = L_FCEQ; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x11: in->lop = L_FCSET; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x12: in->lop = L_FCAND; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x13: in->lop = L_FCOR; in->imm = (int32_t)(w & 0xFFFFFF); return;
    case 0x1C: in->lop = L_FCGET; return;
    case 0x20: in->lop = L_B; return;
    case 0x21: in->lop = L_BAL; return;
    case 0x24: in->lop = L_JR; return;
    case 0x25: in->lop = L_JALR; return;
    case 0x28: in->lop = L_IBEQ; return;
    case 0x29: in->lop = L_IBNE; return;
    case 0x2C: in->lop = L_IBLTZ; return;
    case 0x2D: in->lop = L_IBGTZ; return;
    case 0x2E: in->lop = L_IBLEZ; return;
    case 0x2F: in->lop = L_IBGEZ; return;
    }
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
    }
    vu.decoded = 1;
}

/* ---- State ---------------------------------------------------------------------------------- */
void port_vu1_reset(void) {
    micro = ee_hw_ptr(0x11008000);
    data = ee_hw_ptr(0x1100C000);
    memset(&vu, 0, sizeof(vu));
    vu.vf[0][3] = 0x3F800000u;
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

static void set_q(uint32_t value, int latency) {
    vu.q_next = value;
    vu.q_due = vu.cycle + (uint64_t)latency;
    vu.q_pending = 1;
}

static void set_p(uint32_t value, int latency) {
    vu.p_next = value;
    vu.p_due = vu.cycle + (uint64_t)latency;
    vu.p_pending = 1;
}

static void settle(void) {
    int n, kept = 0;

    if (vu.q_pending && vu.cycle >= vu.q_due) {
        vu.q = vu.q_next;
        vu.q_pending = 0;
    }
    if (vu.p_pending && vu.cycle >= vu.p_due) {
        vu.p = vu.p_next;
        vu.p_pending = 0;
    }
    for (n = 0; n < vu.clip_count; n++) {
        if (vu.clip_queue[n].due <= vu.cycle) {
            vu.clip_visible = vu.clip_queue[n].value;
        } else {
            vu.clip_queue[kept++] = vu.clip_queue[n];
        }
    }
    vu.clip_count = kept;
}

/* Lets everything in flight finish, as the pipelines drain after a program ends. */
static void drain(void) {
    vu.cycle += 64;
    settle();
}

static inline uint32_t div_result(uint32_t num, uint32_t den) {
    float n = as_float(num), d = as_float(den);

    if (d == 0.0f) {
        /* the largest float, with the sign of the quotient (0/0 too) */
        return ((num ^ den) & 0x80000000u) | 0x7F7FFFFFu;
    }
    return as_bits(n / d);
}

static uint32_t efu_length2(const uint32_t *v) {
    float x = as_float(v[0]), y = as_float(v[1]), z = as_float(v[2]);

    return as_bits(x * x + y * y + z * z);
}

/* ---- Running --------------------------------------------------------------------------------- */
static void run(uint32_t pc, uint32_t top, uint32_t itop) {
    int branch_pending = 0;
    uint32_t branch_target = 0;
    int end_next = 0;
    int guard = 0;
    int saved_round = fegetround();
    /* The integer op just before a conditional branch: a branch reads its VI registers a stage
       ahead of where that op writes, so it still sees the old value (the 0x700 program counts its
       loop that way: isubiu vi11, then ibgtz vi11). */
    int last_vi = 0, this_vi = 0;
    uint16_t last_vi_old = 0, this_vi_old = 0;

    if (!vu.decoded) {
        decode_all();
    }
    fesetround(FE_TOWARDZERO);
    for (;;) {
        const VuInsn *in = &code[pc & (MICRO_INSNS - 1)];
        uint64_t need = vu.cycle;
        uint32_t uval[4];
        uint32_t lval[4];
        int lmask = 0;
        int n, f;
        int branch_now = 0;
        uint32_t target = 0;

        last_vi = this_vi;
        last_vi_old = this_vi_old;
        this_vi = 0;

        /* stalls */
        for (n = 0; n < in->nreads; n++) {
            const VfUse *use = &in->reads[n];

            for (f = 0; f < 4; f++) {
                if ((use->mask & (8 >> f)) && vu.ready[use->reg][f] > need) {
                    need = vu.ready[use->reg][f];
                }
            }
        }
        if ((in->lop == L_DIV || in->lop == L_SQRT || in->lop == L_RSQRT || in->lop == L_WAITQ) &&
            vu.q_pending && vu.q_due > need) {
            need = vu.q_due;
        }
        if (in->lop >= L_WAITP && in->lop <= L_ERCPR && vu.p_pending && vu.p_due > need) {
            need = vu.p_due;
        }
        vu.cycle = need;
        settle();

        /* upper, into uval */
        switch (in->uop) {
        case U_ARITH: {
            const uint32_t *a = vu.vf[in->fs];
            for (f = 0; f < 4; f++) {
                uint32_t b;
                float r;

                if (!(in->udest & (8 >> f))) {
                    continue;
                }
                switch (in->bsrc) {
                case B_FT: b = vu.vf[in->ft][f]; break;
                case B_BC: b = vu.vf[in->ft][in->bc]; break;
                case B_Q: b = vu.q; break;
                default: b = vu.i; break;
                }
                switch (in->arith) {
                case A_ADD: r = as_float(a[f]) + as_float(b); uval[f] = as_bits(r); break;
                case A_SUB: r = as_float(a[f]) - as_float(b); uval[f] = as_bits(r); break;
                case A_MUL: r = as_float(a[f]) * as_float(b); uval[f] = as_bits(r); break;
                case A_MADD:
                    r = as_float(vu.acc[f]) + as_float(as_bits(as_float(a[f]) * as_float(b)));
                    uval[f] = as_bits(r);
                    break;
                case A_MSUB:
                    r = as_float(vu.acc[f]) - as_float(as_bits(as_float(a[f]) * as_float(b)));
                    uval[f] = as_bits(r);
                    break;
                case A_MAX: uval[f] = vu_less(a[f], b) ? b : a[f]; break;
                default: uval[f] = vu_less(b, a[f]) ? b : a[f]; break;
                }
            }
            break;
        }
        case U_ITOF: {
            static const float scale[4] = {1.0f, 0.0625f, 1.0f / 4096.0f, 1.0f / 32768.0f};
            for (f = 0; f < 4; f++) {
                if (in->udest & (8 >> f)) {
                    uval[f] = as_bits((float)(int32_t)vu.vf[in->fs][f] * scale[in->bc]);
                }
            }
            break;
        }
        case U_FTOI: {
            static const float scale[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};
            for (f = 0; f < 4; f++) {
                if (in->udest & (8 >> f)) {
                    uval[f] = ftoi(vu.vf[in->fs][f], scale[in->bc]);
                }
            }
            break;
        }
        case U_ABS:
            for (f = 0; f < 4; f++) {
                uval[f] = vu.vf[in->fs][f] & 0x7FFFFFFFu;
            }
            break;
        case U_CLIP: {
            float w = fabsf(as_float(vu.vf[in->ft][3]));
            uint32_t judge = 0;

            for (f = 0; f < 3; f++) {
                float v = as_float(vu.vf[in->fs][f]);

                if (v > w) {
                    judge |= 1u << (f * 2);
                }
                if (v < -w) {
                    judge |= 2u << (f * 2);
                }
            }
            vu.clip = ((vu.clip << 6) | judge) & 0xFFFFFFu;
            if (vu.clip_count < 8) {
                vu.clip_queue[vu.clip_count].value = vu.clip;
                vu.clip_queue[vu.clip_count].due = vu.cycle + 4;
                vu.clip_count++;
            }
            break;
        }
        case U_OPMULA:
        case U_OPMSUB: {
            const uint32_t *s = vu.vf[in->fs], *t = vu.vf[in->ft];
            static const int i1[3] = {1, 2, 0}, i2[3] = {2, 0, 1};
            for (f = 0; f < 3; f++) {
                float prod = as_float(as_bits(as_float(s[i1[f]]) * as_float(t[i2[f]])));

                uval[f] = in->uop == U_OPMULA ? as_bits(prod) : as_bits(as_float(vu.acc[f]) - prod);
            }
            break;
        }
        default:
            break;
        }

        /* lower */
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
                if (in->ls != 0) {
                    vu.vi[in->ls]--;
                }
                address = vu.vi[in->ls];
            } else {
                address = vu.vi[in->ls];
            }
            memcpy(lval, mem_qword(address), 16);
            lmask = in->ldest;
            if (in->lop == L_LQI && in->ls != 0) {
                vu.vi[in->ls]++;
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
                if (in->lt != 0) {
                    vu.vi[in->lt]--;
                }
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
            if (in->lop == L_SQI && in->lt != 0) {
                vu.vi[in->lt]++;
            }
            break;
        }
        case L_ILW:
        case L_ILWR: {
            uint32_t *q = mem_qword((uint32_t)(vu.vi[in->ls] + (in->lop == L_ILW ? in->imm : 0)));
            for (f = 0; f < 4; f++) {
                if (in->ldest & (8 >> f)) {
                    if (in->lt != 0) {
                        vu.vi[in->lt] = (uint16_t)q[f];
                    }
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
#define INT_OP(reg, value)                                                                         \
    do {                                                                                           \
        if (reg) {                                                                                 \
            uint16_t result_ = (uint16_t)(value);                                                  \
            this_vi = (reg);                                                                       \
            this_vi_old = vu.vi[reg];                                                              \
            vu.vi[reg] = result_;                                                                  \
        }                                                                                          \
    } while (0)
        case L_IADDIU: INT_OP(in->lt, vu.vi[in->ls] + in->imm); break;
        case L_ISUBIU: INT_OP(in->lt, vu.vi[in->ls] - in->imm); break;
        case L_IADDI: INT_OP(in->lt, vu.vi[in->ls] + in->imm); break;
        case L_IADD: INT_OP(in->ld, vu.vi[in->ls] + vu.vi[in->lt]); break;
        case L_ISUB: INT_OP(in->ld, vu.vi[in->ls] - vu.vi[in->lt]); break;
        case L_IAND: INT_OP(in->ld, vu.vi[in->ls] & vu.vi[in->lt]); break;
        case L_IOR: INT_OP(in->ld, vu.vi[in->ls] | vu.vi[in->lt]); break;
#undef INT_OP
        case L_MOVE:
            memcpy(lval, vu.vf[in->ls], 16);
            lmask = in->ldest;
            break;
        case L_MR32:
            lval[0] = vu.vf[in->ls][1];
            lval[1] = vu.vf[in->ls][2];
            lval[2] = vu.vf[in->ls][3];
            lval[3] = vu.vf[in->ls][0];
            lmask = in->ldest;
            break;
        case L_MTIR: if (in->lt) vu.vi[in->lt] = (uint16_t)vu.vf[in->ls][in->fsf]; break;
        case L_MFIR:
            for (f = 0; f < 4; f++) {
                lval[f] = (uint32_t)(int32_t)(int16_t)vu.vi[in->ls];
            }
            lmask = in->ldest;
            break;
        case L_DIV:
            set_q(div_result(vu.vf[in->ls][in->fsf], vu.vf[in->lt][in->ftf]), 7);
            break;
        case L_SQRT:
            set_q(as_bits(sqrtf(fabsf(as_float(vu.vf[in->lt][in->ftf])))), 7);
            break;
        case L_RSQRT: {
            uint32_t den = as_bits(sqrtf(fabsf(as_float(vu.vf[in->lt][in->ftf]))));
            set_q(div_result(vu.vf[in->ls][in->fsf], den), 13);
            break;
        }
        case L_WAITQ:
        case L_WAITP:
            break;
        case L_FCAND: vu.vi[1] = (vu.clip_visible & (uint32_t)in->imm) != 0; break;
        case L_FCEQ: vu.vi[1] = vu.clip_visible == (uint32_t)in->imm; break;
        case L_FCOR: vu.vi[1] = ((vu.clip_visible | (uint32_t)in->imm) & 0xFFFFFFu) == 0xFFFFFFu; break;
        case L_FCGET: if (in->lt) vu.vi[in->lt] = (uint16_t)(vu.clip_visible & 0xFFF); break;
        case L_FCSET:
            vu.clip = vu.clip_visible = (uint32_t)in->imm;
            vu.clip_count = 0;
            break;
        case L_B:
        case L_BAL:
            branch_now = 1;
            target = pc + 1 + (uint32_t)in->imm;
            if (in->lop == L_BAL && in->lt) {
                vu.vi[in->lt] = (uint16_t)(pc + 2);
            }
            break;
        case L_JR:
        case L_JALR:
            branch_now = 1;
            target = vu.vi[in->ls];
            if (in->lop == L_JALR && in->lt) {
                vu.vi[in->lt] = (uint16_t)(pc + 2);
            }
            break;
        case L_IBEQ:
        case L_IBNE:
        case L_IBLTZ:
        case L_IBGTZ:
        case L_IBLEZ:
        case L_IBGEZ: {
            uint16_t vs = last_vi && in->ls == last_vi ? last_vi_old : vu.vi[in->ls];
            uint16_t vt = last_vi && in->lt == last_vi ? last_vi_old : vu.vi[in->lt];
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
        case L_XTOP: if (in->lt) vu.vi[in->lt] = (uint16_t)top; break;
        case L_XITOP: if (in->lt) vu.vi[in->lt] = (uint16_t)itop; break;
        case L_XGKICK:
            port_gif_path1_kick(vu.vi[in->ls]);
            break;
        case L_MFP:
            for (f = 0; f < 4; f++) {
                lval[f] = vu.p;
            }
            lmask = in->ldest;
            break;
        case L_ESADD: set_p(efu_length2(vu.vf[in->ls]), 11); break;
        case L_ERSADD: set_p(div_result(0x3F800000u, efu_length2(vu.vf[in->ls])), 18); break;
        case L_ELENG: set_p(as_bits(sqrtf(as_float(efu_length2(vu.vf[in->ls])))), 18); break;
        case L_ERLENG:
            set_p(div_result(0x3F800000u, as_bits(sqrtf(as_float(efu_length2(vu.vf[in->ls]))))), 24);
            break;
        case L_ESQRT: set_p(as_bits(sqrtf(fabsf(as_float(vu.vf[in->ls][in->fsf])))), 12); break;
        case L_ERSQRT:
            set_p(div_result(0x3F800000u, as_bits(sqrtf(fabsf(as_float(vu.vf[in->ls][in->fsf]))))), 18);
            break;
        case L_ERCPR: set_p(div_result(0x3F800000u, vu.vf[in->ls][in->fsf]), 12); break;
        case L_ESUM: {
            const uint32_t *v = vu.vf[in->ls];
            set_p(as_bits(as_float(v[0]) + as_float(v[1]) + as_float(v[2]) + as_float(v[3])), 12);
            break;
        }
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
            lmask = in->ldest;
            break;
        default:
            PORT_TODO("VU1: an instruction the interpreter does not know");
            break;
        }

        /* write back: the upper first; the lower's VF write is dropped if it is the same register */
        if (in->uwrite != REG_NONE && in->uop != U_NOP && in->uop != U_CLIP) {
            uint32_t *d = in->uwrite == REG_ACC ? vu.acc : vu.vf[in->uwrite];

            if (in->uwrite != 0) {
                for (f = 0; f < 4; f++) {
                    if (in->udest & (8 >> f)) {
                        d[f] = uval[f];
                        if (in->uwrite != REG_ACC) {
                            vu.ready[in->uwrite][f] = vu.cycle + 4;
                        }
                    }
                }
            }
        }
        if (lmask && in->lwrite != REG_NONE && in->lwrite != 0 &&
            !(in->uwrite == in->lwrite && in->uop != U_NOP)) {
            for (f = 0; f < 4; f++) {
                if (lmask & (8 >> f)) {
                    vu.vf[in->lwrite][f] = lval[f];
                    vu.ready[in->lwrite][f] = vu.cycle + 4;
                }
            }
        }
        if (in->lop == L_LOI) {
            vu.i = in->i_value;
        }
        vu.cycle++;

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
    drain();
    fesetround(saved_round);
}

void port_vu1_start(uint32_t address, uint32_t top, uint32_t itop) {
    run(address, top, itop);
}

void port_vu1_continue(uint32_t top, uint32_t itop) {
    run(vu.pc, top, itop);
}
