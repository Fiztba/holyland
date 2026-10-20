/* VU1's program 0x000 without the interpreter ("HLE"): the same results, computed directly.

   Program 0x000 (game/data/vudata.c, vu1_packed_vertex_program; tools/vu1_disasm.py prints it) draws
   86% of the game's triangles: map records, posed models, items. It is 289 instruction pairs at
   0x000-0x120, software-pipelined three vertices deep, and the interpreter (vu1.c) spends most of
   its time on it. This computes what it computes, a vertex at a time, to the bit: the same XGKICK
   packets, the same data memory, and the same registers, flags and clock at the end, so that the
   interpreter can take the next run (another program, or this one) as if it had run this one. It
   is also the specification of the vertex shader that is to replace it (docs/port/STEP3.md).

   What the program does
   - MSCAL 0x000 (once per model part): the set-up. It multiplies the model matrix (VU 0x04-0x07) by
     the view matrix (0x00-0x03) into the vertex matrix (0x14-0x17), the light directions (0x0C-0x0F)
     by the model's rotation, moves up to four point lights (0x41-0x44, count in 0x40.w) into model
     space (0x4A-0x4D, colours copied to 0x4E-0x51, the count to 0x49), loads the light colours and
     ambient (0x10-0x13) and the constants (255, the 2048 screen offset, the 2047 guard band), and
     kicks the FOGCOL packet at 0x3D. Then it goes on as MSCNT does.
   - MSCNT (0x11F: `b 0x060`): one batch at TOP. TOP+0 is the strip's GIF tag (NLOOP = n vertices),
     TOP+1+4k vertex k: position (int16 x3 + w, ITOF0), normal (ITOF12; its w's low 16 bits are ORed
     into XYZF2's w: the model's own ADC bit), ST (ITOF12, z = 1.0), colour (ITOF0). Per vertex:
       c = M * pos; q = 1 / c.w; xyz = c.xyz * q; fog = fog.z + fog.w * q, clamped to 0..255;
       dots = max(L * normal, 0); colour = min((C * dots + ambient) * vertex colour, 255), alpha
       1 * vertex alpha; ST = st * q (z: Q for the GS).
     Out at TOP+0xC9+3k: ST, RGBAQ (FTOI0), XYZF2 (FTOI4, w = fog << 4 | ADC | the normal's w).
     A vertex is "clipped" when (xyz - (2048, 2048, 0)) is outside +-2047 in x or y, or z < 0; ADC
     is set when it or either of the two before it is clipped. The strip's tag is copied to
     TOP+0xC8, the A+D packet at TOP+0xA0 (from the model data) is kicked, then each point light
     adds its colour * max(1 - |light - pos|^2 * light.w, 0) to every vertex's RGBAQ in memory (a
     pass per light, clamped to 255), and the strip at TOP+0xC8 is kicked.
   - The pipeline reaches past the batch at both ends, and that is visible: a "vertex -1" is clip
     tested (vertex 0 scaled by 1 / the previous run's vf31.w), and its result counts in vertex 0's
     and 1's ADC; registers the previous run left are stored at TOP+0xC6/0xC7 and, per light pass,
     TOP+0xC1/0xC4; the light passes read TOP-3 and two vertices past the end, and write those.
     All of it is reproduced here.

   Timing. VF results are always seen new (a read stalls until they are due), so only Q, P and the
   clip flag, which are not stalled for, depend on the clock. The program's timing is fixed (its
   stalls are static; checked against a trace of the interpreter), and every DIV result is due by
   the MULQ that reads it, every ESADD's by its MFP, every CLIP by its FCAND, except one: the FCAND
   at 0x07E sees only the first of the two CLIPs before it (vertex -1's guard band; with the tag's w
   it decides whether every vertex gets ADC: the game's tags have w = 0). The cycles and pairs a run
   takes are a formula in n and the light count (the end of vu1_hle_run).

   Arithmetic. Each FMAC operation is the interpreter's (v4_op: IEEE floats rounded toward zero,
   denormals as zero, exponent 255 an ordinary number), in the program's order, and its flags are
   kept as the interpreter keeps them. That is the "exact" form, and the specification. On x86 a
   vertex is first done the "fast" way, in plain SSE floats with flush-to-zero: when no operand has
   exponent 255 and no operation overflows or underflows (MXCSR's O, U and I flags stay clear), every
   result is the same, and the only flags it would have raised are Z and S, which then must already
   be set in the status flag's sticky bits (they are, after the first runs). Otherwise the vertex is
   done again the exact way.

   Not handled here, left to the interpreter: n < 3 (the loop would run 65536 times), n > 39 (the
   vertices would reach the A+D packet at TOP+0xA0, and the output its input), a TOP whose batch
   wraps around data memory or reaches into the fixed block, anything in flight at the start, and
   micro memory with an FSxxx/FMxxx in it (the interpreter then queues flag writes). The game's
   batches have n = 3..36 at TOP 0x80 or 0x240.

   Only the DMA controller's thread calls in here, through vu1.c's run. */
#include <stdint.h>
#include <string.h>

#include "vu1_internal.h"

#define PROGRAM_PAIRS 0x121 /* 0x000-0x120 */
#define PROGRAM_HASH 0xABC9F5BDEEEE9587ull /* FNV-1a 64 of those pairs as uploaded */
#define ENTRY_MSCNT 0x11F
#define N_MIN 3
#define N_MAX 39

/* the I register's values (the program's LOIs) */
#define I_2047 0x44FFE000u
#define I_255 0x437F0000u
#define I_2_24 0x4B800000u
#define I_2048 0x45000000u
#define I_ONE 0x3F800000u

#define LANES_ALL field_lanes[15]
#define LANES_XYZ field_lanes[14]
#define LANES_W field_lanes[1]

#define ALWAYS_INLINE inline __attribute__((always_inline))

#if VU_DAZ
#define MXCSR_FTZ 0x8000u
#define MXCSR_FLAGS 0x003Fu
#define MXCSR_TROUBLE 0x0019u /* invalid, overflow, underflow */
#endif

static int recognised; /* program 0x000; the others' files keep their own */

int vu1_hle_recognise(const uint8_t *micro) {
    uint64_t hash = 0xCBF29CE484222325ull;
    int n, others = 0;

    for (n = 0; n < PROGRAM_PAIRS * 8; n++) {
        hash = (hash ^ micro[n]) * 0x100000001B3ull;
    }
    recognised = hash == PROGRAM_HASH;
    others |= vu1_hle_5a0_recognise(micro);
    others |= vu1_hle_700_recognise(micro);
    return recognised || others;
}

/* ---- Arithmetic ------------------------------------------------------------------------------ */
/* The flags the run's FMAC instructions raise, as the interpreter keeps them when nothing in micro
   memory reads them (vu1.c's interpret): Z U O of them all, and the signs of their results. */
typedef struct {
    unsigned zuo;
    V4 signs;
} Flags;

/* a op b on the fields in lanes (the others are undefined): exact, the interpreter's, with its
   flags; or fast (see the top) */
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

/* ---- MSCAL 0x000: the set-up (0x000-0x05F), in the program's order ----------------------------- */
static void setup(Vu1State *vu, uint8_t *data, Flags *fl) {
    V4 vf[32], acc, zero, m[4], r[3], p;
    int n;

    for (n = 0; n < 32; n++) {
        vf[n] = v4_load(vu->vf[n]);
    }
    /* 000-007: the constants' registers to 0 (vf00 - vf00), the view matrix and model matrix */
    zero = fop(fl, 1, A_SUB, vf[0], vf[0], LANES_ALL);
    vf[19] = vf[18] = vf[17] = vf[24] = zero;
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)n);
        vf[5 + n] = load(data, (uint32_t)(4 + n));
    }
    /* 005-018: the vertex matrix, view * model, to 0x14-0x17; the FOGCOL packet is kicked after the
       third row is stored and before the fourth */
    for (n = 0; n < 4; n++) {
        m[n] = vf[1 + n];
    }
    for (n = 0; n < 4; n++) {
        vf[9 + n] = transform4(fl, 1, m, vf[5 + n]);
    }
    store(data, 0x14, vf[9]);
    store(data, 0x15, vf[10]);
    vf[1] = load(data, 0x40); /* 012 */
    vf[18] = merge(vf[18], fop(fl, 1, A_ADD, vf[18], v4_splat(I_2047), LANES_W), LANES_W); /* 015 */
    vu->vi[11] = (uint16_t)qword(data, 0x40)[3];
    store(data, 0x16, vf[11]);
    vu->vi[14] = 0x3D;
    vu1_xgkick(0x3D); /* 017 */
    store(data, 0x17, vf[12]);
    /* 01A-024: the point lights, less the model's translation, by the model's rotation transposed */
    vf[10] = fop(fl, 1, A_ADD, vf[24], vf[5], LANES_ALL);
    vf[11] = fop(fl, 1, A_ADD, vf[24], vf[6], LANES_ALL);
    vf[12] = fop(fl, 1, A_ADD, vf[24], vf[7], LANES_ALL);
    vf[13] = fop(fl, 1, A_ADD, vf[24], vf[8], LANES_ALL);
    for (n = 0; n < 4; n++) {
        vf[20 + n] = load(data, (uint32_t)(0x41 + n));
    }
    store(data, 0x49, vf[1]); /* 01E: the light count, and what else 0x40 holds */
    vf[10] = merge(vf[10], fop(fl, 1, A_SUB, vf[24], vf[10], field_lanes[6]), field_lanes[6]);
    vf[11] = merge(vf[11], fop(fl, 1, A_SUB, vf[24], vf[11], field_lanes[10]), field_lanes[10]);
    vf[12] = merge(vf[12], fop(fl, 1, A_SUB, vf[24], vf[12], field_lanes[12]), field_lanes[12]);
    for (n = 0; n < 4; n++) {
        vf[20 + n] = merge(vf[20 + n], fop(fl, 1, A_SUB, vf[20 + n], vf[13], LANES_XYZ), LANES_XYZ);
    }
    /* 025-04C: each light (x, y and z each a MULAx; MADDAy; MADDz of their own, w = 1 * w), to
       0x4A-0x4D; their colours, 0x45-0x48, to 0x4E-0x51 */
    r[0] = vf[10];
    r[1] = vf[11];
    r[2] = vf[12];
    for (n = 0; n < 4; n++) {
        p = vf[20 + n];
        acc = fop(fl, 1, A_MUL, r[0], splat(p, 0), LANES_XYZ);
        acc = fmadd(fl, 1, acc, r[1], splat(p, 1), LANES_XYZ);
        vf[25 + n] = merge(vf[25 + n], fmadd(fl, 1, acc, r[2], splat(p, 2), LANES_XYZ), LANES_XYZ);
        vf[25 + n] = merge(vf[25 + n], fop(fl, 1, A_MUL, vf[0], splat(p, 3), LANES_W), LANES_W);
    }
    store(data, 0x4A, vf[25]);
    store(data, 0x4B, vf[26]);
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)(0x45 + n));
    }
    for (n = 0; n < 4; n++) {
        store(data, (uint32_t)(0x4E + n), vf[1 + n]);
    }
    store(data, 0x4C, vf[27]);
    /* 049-058: the light directions (0x0C-0x0F) by the model's rotation */
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)(0x0C + n));
        m[n] = vf[1 + n];
    }
    vf[5] = transform4(fl, 1, m, vf[5]);
    store(data, 0x4D, vf[28]); /* 050 */
    vf[6] = transform4(fl, 1, m, vf[6]);
    /* the last one's MULAx; MADDAy; MADDAz leave the accumulator as the set-up ends */
    acc = fop(fl, 1, A_MUL, m[0], splat(vf[7], 0), LANES_ALL);
    acc = fmadd(fl, 1, acc, m[1], splat(vf[7], 1), LANES_ALL);
    acc = fmadd(fl, 1, acc, m[2], splat(vf[7], 2), LANES_ALL);
    vf[7] = fmadd(fl, 1, acc, m[3], splat(vf[7], 3), LANES_ALL);
    /* 056-05F: the vertex matrix and the light colours and ambient back; the constants */
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)(0x14 + n));
        vf[8 + n] = load(data, (uint32_t)(0x10 + n));
    }
    vf[19] = fop(fl, 1, A_ADD, vf[19], v4_splat(I_255), LANES_ALL);
    vf[17] = merge(vf[17], fop(fl, 1, A_ADD, vf[17], v4_splat(I_2_24), LANES_W), LANES_W);
    vf[18] = merge(vf[18], fop(fl, 1, A_ADD, vf[18], v4_splat(I_2048), field_lanes[12]), field_lanes[12]);
    for (n = 1; n < 32; n++) {
        v4_store(vu->vf[n], vf[n]);
    }
    v4_store(vu->vf[REG_ACC], acc);
    vu->i = I_2048;
    vu->vi[1] = 0x18;
    vu->vi[2] = 0x0C;
    vu->vi[3] = 0x14;
    vu->vi[4] = 0x40;
}

/* ---- MSCNT: a batch --------------------------------------------------------------------------- */
/* What stays the same over a batch: registers the set-up loaded, and the fog */
typedef struct {
    V4 m[4];      /* vf01-vf04: the vertex matrix */
    V4 l[3];      /* vf05-vf07: the light directions */
    V4 c[3];      /* vf08-vf10: the light colours */
    V4 ambient;   /* vf11 */
    V4 fog;       /* vf12: VU 0x3C */
    V4 offset;    /* vf18: (2048, 2048, 0, the guard band 2047) */
    V4 limit;     /* vf19: 255 */
    V4 floor;     /* vf24: 0 */
    uint32_t one; /* vf00.w */
    int fast;     /* the fast way may be tried (see the top) */
#if VU_DAZ
    unsigned csr; /* MXCSR in the run (toward zero, DAZ) */
#endif
} Batch;

/* One vertex of the strip, everything a later stage or the end state needs */
typedef struct {
    V4 c;        /* vf31 after the transform */
    V4 nf;       /* the normal (ITOF12) */
    V4 sf;       /* ST (ITOF12) */
    V4 cf;       /* the colour (ITOF0) */
    V4 dots;     /* vf22: max(L * normal, 0) */
    V4 acc;      /* ACC after the colour's MADDAy, with w = 1 * fog.z */
    V4 proj;     /* vf31 at the end: xyz * q, fog clamped */
    V4 rel;      /* vf30.xyz: proj - offset */
    V4 st;       /* vf29.xyz: ST * q */
    V4 scr;      /* XYZF2 (FTOI4 of proj) */
    V4 colour;   /* RGBAQ (FTOI0) */
    V4 last;     /* the colour's MUL (the run's last FMAC result when there are no point lights) */
    unsigned last_zuo;
    uint32_t q, q_status;
    uint32_t judgements; /* guard band << 6 | zero */
} Vertex;

/* Vertex k from its four qwords at address (TOP+1+4k), in the program's order of operations. 0
   when the fast way cannot be trusted with it (nothing kept then). */
static ALWAYS_INLINE int vertex_as(Flags *fl, int exact, const Batch *b, uint8_t *data, uint32_t address, Vertex *v) {
    V4 acc, lit, fogw, proj;

    v->c = transform4(fl, exact, b->m, v4_itof(load(data, address), 0));
    v->nf = v4_itof(load(data, address + 1), 12);
    v->sf = v4_itof(load(data, address + 2), 12);
    v->cf = v4_itof(load(data, address + 3), 0);
    /* the light dots */
    acc = fop(fl, exact, A_MUL, b->l[0], splat(v->nf, 0), LANES_ALL);
    acc = fmadd(fl, exact, acc, b->l[1], splat(v->nf, 1), LANES_ALL);
    v->dots = v4_max(fmadd(fl, exact, acc, b->l[2], splat(v->nf, 2), LANES_ALL), b->floor);
    /* 1 / w: DIV Q, vf00w, vf31w */
    v->q = vf_div(b->one, v->c[3]);
    v->q_status = div_status(b->one, v->c[3]);
    if (!exact && (v->q & 0x7F800000u) == 0x7F800000u) {
        return 0;
    }
    /* the lit colour: C * dots, w = 0 + 1 (ADDI.w with I = 1) */
    acc = fop(fl, exact, A_MUL, b->c[0], splat(v->dots, 0), LANES_ALL);
    acc = fmadd(fl, exact, acc, b->c[1], splat(v->dots, 1), LANES_ALL);
    lit = fmadd(fl, exact, acc, b->c[2], splat(v->dots, 2), LANES_ALL);
    lit = merge(lit, fop(fl, exact, A_ADD, b->floor, v4_splat(I_ONE), LANES_W), LANES_W);
    /* the fog's ACC.w = 1 * fog.z (MULAz.w), then the perspective */
    fogw = fop(fl, exact, A_MUL, v4_splat(b->one), splat(b->fog, 2), LANES_W);
    v->acc = merge(acc, fogw, LANES_W);
    proj = fop(fl, exact, A_MUL, v->c, v4_splat(v->q), LANES_XYZ);
    v->st = fop(fl, exact, A_MUL, v->sf, v4_splat(v->q), LANES_XYZ);
    proj = merge(proj, fmadd(fl, exact, fogw, b->fog, v4_splat(v->q), LANES_W), LANES_W);
    lit = merge(lit, fop(fl, exact, A_ADD, lit, b->ambient, LANES_XYZ), LANES_XYZ);
    v->rel = fop(fl, exact, A_SUB, proj, b->offset, LANES_XYZ);
    proj[3] = vf_max(vf_mini(proj[3], b->limit[3]), b->floor[3]);
    v->proj = proj;
    /* the colour's MUL, the interpreter's always: its flags may be the run's last */
    v->last_zuo = 0;
    v->last = v4_op(A_MUL, lit, v->cf, LANES_ALL, &v->last_zuo);
    if (exact) {
        fl->zuo |= v->last_zuo;
        fl->signs |= v->last;
    }
    v->colour = v4_ftoi(v4_mini(v->last, b->limit), 0);
    v->scr = v4_ftoi(proj, 4);
    v->judgements = judge(v->rel, b->offset[3]) << 6 | judge(v->rel, b->floor[3]);
    return 1;
}

static void vertex(Flags *fl, const Batch *b, uint8_t *data, uint32_t address, Vertex *v) {
#if VU_DAZ
    if (b->fast) {
        int done;

        _mm_setcsr((b->csr & ~MXCSR_FLAGS) | MXCSR_FTZ);
        done = vertex_as(fl, 0, b, data, address, v);
        done &= (_mm_getcsr() & MXCSR_TROUBLE) == 0;
        _mm_setcsr(b->csr);
        if (done) {
            return;
        }
    }
#endif
    vertex_as(fl, 1, b, data, address, v);
}

/* the vertex's three qwords at slot (TOP+0xC9+3k); xyzw: XYZF2's w (VI arithmetic, 16 bits) */
static inline void vertex_store(uint8_t *data, uint32_t slot, const Vertex *v, uint32_t st_w, uint16_t xyzw) {
    V4 st = v->st, xyz = v->scr;

    st[3] = st_w;
    xyz[3] = xyzw;
    store(data, slot, st);
    store(data, slot + 1, v->colour);
    store(data, slot + 2, xyz);
}

/* ---- The point lights (0x0DF-0x119) ------------------------------------------------------------ */
/* What a light pass leaves in the registers that the next pass, or the end state, reads */
typedef struct {
    V4 vf20, vf21, vf22, vf23, vf26, vf27, vf28, vf29, vf30, light, colour;
    uint32_t p;
    V4 last;          /* the pass's last ADD (vf29) */
    unsigned last_zuo;
} LightEnd;

/* A pass's light, for one vertex */
typedef struct {
    V4 light, colour; /* vf13, vf14 */
    uint32_t w23;     /* vf23.w: min(vf22.w, 0), the same for the whole pass */
    int fast;
} Light;

typedef struct {
    V4 add;  /* vf28: 0 + its RGBAQ (ITOF0) */
    V4 att;  /* vf21.w: 1 - |light - pos|^2 * light.w */
    V4 a22;  /* vf22.xyz: colour * att */
    V4 a23;  /* vf23: max(a22, 0), w23 */
    V4 sum;  /* vf29: add + a23 */
    unsigned sum_zuo;
} LightVertex;

/* vertex k's RGBAQ (raw, as in memory) and position (pf, ITOF0) through the light. 0 when the fast
   way cannot be trusted with it. */
static ALWAYS_INLINE int light_vertex_as(Flags *fl, int exact, const Batch *b, const Light *l, V4 raw, V4 pf,
                                         LightVertex *lv) {
    V4 d = fop(fl, exact, A_SUB, l->light, pf, LANES_XYZ);
    uint32_t p = sum_squares(exact, d);

    lv->add = fop(fl, exact, A_ADD, b->floor, v4_itof(raw, 0), LANES_ALL);
    lv->att = fop(fl, exact, A_MUL, v4_splat(p), l->light, LANES_W);
    lv->att = fop(fl, exact, A_SUB, v4_splat(b->one), lv->att, LANES_W);
    lv->a22 = fop(fl, exact, A_MUL, l->colour, splat(lv->att, 3), LANES_XYZ);
    lv->a23 = v4_max(lv->a22, b->floor);
    lv->a23[3] = l->w23;
    /* the interpreter's always: its flags may be the run's last */
    lv->sum_zuo = 0;
    lv->sum = v4_op(A_ADD, lv->add, lv->a23, LANES_ALL, &lv->sum_zuo);
    if (exact) {
        fl->zuo |= lv->sum_zuo;
        fl->signs |= lv->sum;
    }
    return 1;
}

static void light_vertex(Flags *fl, const Batch *b, const Light *l, V4 raw, V4 pf, LightVertex *lv) {
#if VU_DAZ
    if (l->fast) {
        int done;

        _mm_setcsr((b->csr & ~MXCSR_FLAGS) | MXCSR_FTZ);
        done = light_vertex_as(fl, 0, b, l, raw, pf, lv);
        done &= (_mm_getcsr() & MXCSR_TROUBLE) == 0;
        _mm_setcsr(b->csr);
        if (done) {
            return;
        }
    }
#endif
    light_vertex_as(fl, 1, b, l, raw, pf, lv);
}

/* One point light's pass over the strip's RGBAQ in memory: vertices -1 .. n+1, and two more stores
   of what the registers held before the pass (slots -3 and -2). */
static void light_pass(Flags *fl, const Batch *b, uint8_t *data, uint32_t top, uint32_t n, uint32_t index,
                       LightEnd *e) {
    Light l;
    LightVertex lv;
    V4 add, att, a22, a23, sum, d, pf;
    int32_t k;

    l.light = load(data, 0x4A + index);
    l.colour = load(data, 0x4E + index);
    l.w23 = vf_mini(e->vf22[3], b->floor[3]);
    l.fast = b->fast && !has_huge(l.light) && !has_huge(l.colour) && (l.w23 & 0x7F800000u) != 0x7F800000u;
    /* slot -3: vf29 as the pass found it; slot -2: vf27 + the light at vf21.w as it found them */
    store(data, top + 0xC1, v4_ftoi(v4_mini(e->vf29, b->limit), 0));
    add = fop(fl, 1, A_ADD, b->floor, e->vf27, LANES_ALL);
    att = fop(fl, 1, A_SUB, v4_splat(b->one), e->vf21, LANES_W);
    a22 = fop(fl, 1, A_MUL, l.colour, splat(att, 3), LANES_XYZ);
    a23 = v4_max(a22, b->floor);
    a23[3] = l.w23;
    sum = fop(fl, 1, A_ADD, add, a23, LANES_ALL);
    store(data, top + 0xC4, v4_ftoi(v4_mini(sum, b->limit), 0));
    /* the vertices: RGBAQ += colour * max(1 - |light - pos|^2 * light.w, 0); vertex -1's RGBAQ is
       the main loop's store of vf20 at TOP+0xC7, its position TOP-3 */
    for (k = -1; k <= (int32_t)n + 1; k++) {
        uint32_t slot = top + 0xCA + 3 * (uint32_t)k;

        light_vertex(fl, b, &l, load(data, slot), v4_itof(load(data, top + 1 + 4 * (uint32_t)k), 0), &lv);
        store(data, slot, v4_ftoi(v4_mini(lv.sum, b->limit), 0));
    }
    /* the pipeline's reach past the strip: vertices n+2 and n+3 are loaded and subtracted, n+2's
       distance is the last ESADD, and n+2's RGBAQ is loaded and converted */
    pf = v4_itof(load(data, top + 1 + 4 * (n + 2)), 0);
    d = fop(fl, 1, A_SUB, l.light, pf, LANES_XYZ);
    e->p = sum_squares(1, d);
    pf = v4_itof(load(data, top + 1 + 4 * (n + 3)), 0);
    e->vf20 = merge(e->vf20, fop(fl, 1, A_SUB, l.light, pf, LANES_XYZ), LANES_XYZ);
    e->vf26 = load(data, top + 0xCA + 3 * (n + 2));
    e->vf27 = v4_itof(e->vf26, 0);
    e->vf28 = lv.add;
    e->vf21 = merge(e->vf21, lv.att, LANES_W);
    e->vf22 = merge(e->vf22, lv.a22, LANES_XYZ);
    e->vf23 = lv.a23;
    e->vf29 = lv.sum;
    e->last = lv.sum;
    e->last_zuo = lv.sum_zuo;
    e->vf30 = v4_ftoi(v4_mini(lv.sum, b->limit), 0);
    e->light = l.light;
    e->colour = l.colour;
}

/* ---- The run ---------------------------------------------------------------------------------- */
uint64_t vu1_hle_run(Vu1State *vu, uint8_t *data, uint32_t pc, uint32_t top) {
    Flags fl = {0, {0, 0, 0, 0}};
    Batch b;
    Vertex v[2]; /* vertex k and k-1 */
    Vertex *cur = &v[0], *prev = &v[1];
    V4 vf20_in, vf29_in, vf30_in, rel, mac_value;
    uint32_t n, k, judgements_junk, q_junk, q_status_all, adc_tag, count, lights_word, lights;
    unsigned mac_zuo;
    int clip1, clip2; /* the clip of vertex k-1 and k-2 */
    int mscal = pc == 0;
    uint64_t cycles, pairs;
    int16_t light_count;
    LightEnd e;

    if (pc == 0x5A0 || pc == 0x640) {
        return vu1_hle_5a0_run(vu, data, pc, top);
    }
    if (pc >= 0x700) {
        return vu1_hle_700_run(vu, data, pc, top);
    }
    if (!recognised || (pc != 0 && pc != ENTRY_MSCNT) || vu->q_pending || vu->p_pending || vu->flag_count || vu->flags_read) {
        return 0;
    }
    top &= DATA_QWORDS - 1;
    n = qword(data, top)[0] & 0x7FFF;
    if (n < N_MIN || n > N_MAX || top < 0x80 || top + 0xD1 + 3 * n > DATA_QWORDS) {
        return 0;
    }
    if (mscal) {
        setup(vu, data, &fl);
    }

    /* 060-07F: the batch's start */
    for (k = 0; k < 4; k++) {
        b.m[k] = v4_load(vu->vf[1 + k]);
    }
    for (k = 0; k < 3; k++) {
        b.l[k] = v4_load(vu->vf[5 + k]);
        b.c[k] = v4_load(vu->vf[8 + k]);
    }
    b.ambient = v4_load(vu->vf[11]);
    b.fog = load(data, 0x3C);
    b.offset = v4_load(vu->vf[18]);
    b.limit = v4_load(vu->vf[19]);
    b.floor = v4_load(vu->vf[24]);
    b.one = vu->vf[0][3];
    b.fast = 0;
#if VU_DAZ
    b.csr = _mm_getcsr();
    /* the fast way raises no Z or S of its own: they must be in the sticky bits already */
    b.fast = (vu->status & 0xC0u) == 0xC0u && !has_huge(b.m[0]) && !has_huge(b.m[1]) && !has_huge(b.m[2]) &&
             !has_huge(b.m[3]) && !has_huge(b.l[0]) && !has_huge(b.l[1]) && !has_huge(b.l[2]) &&
             !has_huge(b.c[0]) && !has_huge(b.c[1]) && !has_huge(b.c[2]) && !has_huge(b.ambient) &&
             !has_huge(b.fog) && !has_huge(b.offset) && !has_huge(b.limit) && !has_huge(b.floor) &&
             !has_huge(v4_splat(b.one));
#endif
    vf20_in = v4_load(vu->vf[20]);
    vf29_in = v4_load(vu->vf[29]);
    vf30_in = v4_load(vu->vf[30]);

    vertex(&fl, &b, data, top + 1, cur);
    /* "Vertex -1": the first DIV divides by the vf31.w the last run left, and vertex 0's transform
       is scaled by it and clip-tested. The FCAND at 0x07E sees only its guard-band CLIP's -z; with
       the tag's w that decides whether every vertex gets ADC. */
    q_junk = vf_div(b.one, vu->vf[31][3]);
    q_status_all = div_status(b.one, vu->vf[31][3]);
    rel = fop(&fl, 1, A_SUB, fop(&fl, 1, A_MUL, cur->c, v4_splat(q_junk), LANES_XYZ), b.offset, LANES_XYZ);
    judgements_junk = judge(rel, b.offset[3]) << 6 | judge(rel, b.floor[3]);
    adc_tag = (uint16_t)qword(data, top)[3] & ((judgements_junk & (0x20u << 6)) != 0);
    /* the first loop pass stores vf29 and vf20 as they were (vertex -1's ST and RGBAQ; its XYZF2,
       vf21, goes to TOP+0xC8, where the tag replaces it before anything reads it) */
    store(data, top + 0xC6, vf29_in);
    store(data, top + 0xC7, vf20_in);

    /* 080-0DB: the vertices; each one's ADC is its clip and the two before it */
    clip1 = clipped(judgements_junk);
    clip2 = 0;
    count = (uint32_t)clip1;
    for (k = 0;; k++) {
        uint32_t nw = qword(data, top + 2 + 4 * k)[3];
        int clip;
        uint16_t bits;

        q_status_all |= cur->q_status;
        clip = clipped(cur->judgements);
        count += (uint32_t)clip;
        bits = (uint16_t)((((uint32_t)(clip | clip1 | clip2) | adc_tag) + 0x7FFF) & 0x8000) | (uint16_t)nw;
        if (k == n - 1) {
            /* 0C7: the tag to TOP+0xC8, and the A+D packet is kicked before the last vertex's stores */
            store(data, top + 0xC8, load(data, top));
            vu1_xgkick(top + 0xA0);
            vertex_store(data, top + 0xC9 + 3 * k, cur, vf29_in[3], (uint16_t)((uint16_t)cur->scr[3] | bits));
            vu->vi[9] = bits;
            vu->vi[15] = (uint16_t)nw;
            break;
        }
        vertex_store(data, top + 0xC9 + 3 * k, cur, vf29_in[3], (uint16_t)((uint16_t)cur->scr[3] | bits));
        clip2 = clip1;
        clip1 = clip;
        {
            Vertex *t = prev;

            prev = cur;
            cur = t;
        }
        vertex(&fl, &b, data, top + 1 + 4 * (k + 1), cur);
    }
    mac_value = cur->last;
    mac_zuo = cur->last_zuo;

    /* 0DC-11A: the point lights; then the strip is kicked */
    lights_word = qword(data, 0x49)[3];
    light_count = (int16_t)lights_word;
    lights = light_count > 0 ? (uint32_t)light_count : 0;
    memset(&e, 0, sizeof(e));
    e.vf20 = cur->colour;
    e.vf21 = cur->scr;
    e.vf22 = cur->dots;
    e.vf23 = v4_load(vu->vf[23]);
    e.vf26 = cur->nf;
    e.vf27 = cur->sf;
    e.vf28 = cur->cf;
    e.vf29 = cur->st;
    e.vf29[3] = vf29_in[3];
    e.vf30 = cur->rel;
    e.vf30[3] = vf30_in[3];
    for (k = 0; k < lights; k++) {
        light_pass(&fl, &b, data, top, n, k, &e);
        mac_value = e.last;
        mac_zuo = e.last_zuo;
    }
    vu1_xgkick(top + 0xC8); /* 11B */

    /* the registers as the program leaves them */
    v4_store(vu->vf[12], b.fog);
    v4_store(vu->vf[20], e.vf20);
    v4_store(vu->vf[21], e.vf21);
    v4_store(vu->vf[22], e.vf22);
    v4_store(vu->vf[23], e.vf23);
    v4_store(vu->vf[25], load(data, top - 3)); /* 0E1, in the delay slot of the last IBLEZ */
    v4_store(vu->vf[26], e.vf26);
    v4_store(vu->vf[27], e.vf27);
    v4_store(vu->vf[28], e.vf28);
    v4_store(vu->vf[29], e.vf29);
    v4_store(vu->vf[30], e.vf30);
    v4_store(vu->vf[31], cur->proj);
    v4_store(vu->vf[REG_ACC], cur->acc);
    if (lights) {
        v4_store(vu->vf[13], e.light);
        v4_store(vu->vf[14], e.colour);
        vu->p = vu->p_next = e.p;
    }
    vu->q = vu->q_next = cur->q;
    vu->q_status_next = cur->q_status;
    vu->i = I_ONE;
    vu->vi[1] = (uint16_t)(0x4A + lights);
    vu->vi[2] = (uint16_t)(0x4E + lights);
    vu->vi[3] = (uint16_t)(lights ? top + 0xD6 + 3 * n : 0x8000);
    vu->vi[4] = (uint16_t)(top + 0xC8);
    vu->vi[5] = (uint16_t)top;
    vu->vi[6] = (uint16_t)(count - (n + 2)); /* never >= 0, so the IBGEZ at 0x0D3 is never taken */
    vu->vi[7] = (uint16_t)(top + 0x7FFD);
    vu->vi[8] = (uint16_t)clip1;
    vu->vi[10] = (uint16_t)adc_tag;
    vu->vi[11] = (uint16_t)(lights ? 0 : lights_word);
    vu->vi[12] = (uint16_t)(lights ? 0xFFFF : top + 0xC6 + 3 * n);
    vu->vi[13] = (uint16_t)(n + 2);
    /* the clip flag holds the last two vertices' judgements */
    vu->clip = vu->clip_visible = (prev->judgements << 12 | cur->judgements) & 0xFFFFFFu;
    /* the status flag: the last DIV's I and D, and the sticky bits of all of them; then the FMAC
       flags as vu1.c's interpret leaves them (the last FMAC result's, and the sticky Z S U O of all) */
    vu->status = (vu->status & ~0x30u) | cur->q_status | (q_status_all << 6);
    {
        V4 sign = (mac_value >> 31) & LANES_ALL;
        unsigned mac = mac_zuo | (sign[0] << 7) | (sign[1] << 6) | (sign[2] << 5) | (sign[3] << 4);
        unsigned seen = mac_zsuo(fl.zuo | mac_zuo) | (v4_any((fl.signs | mac_value) & SIGN) ? 2u : 0);

        vu->mac = mac;
        vu->mac_queued = (int32_t)mac;
        vu->status = (vu->status & ~0xFu) | mac_zsuo(mac) | (seen << 6);
    }
    /* The clock: the set-up is 96 cycles (MSCNT's branch 2); 0x060-0x07F 33; each loop pass 32 and
       the last 31; 0x09F-0x0DF 67; each light 46 + 14 a vertex over n + 3; the end 7 and the last
       pair's cycle; then vu1.c's drain. The pairs run: 96 or 2; 32; 30 a loop pass (n - 2); 65; each
       light 46 + 14 (n + 3); 8. */
    cycles = (mscal ? 96u : 2u) + 33 + 32 * (uint64_t)(n - 3) + 31 + 67 + lights * (46 + 14 * (uint64_t)(n + 3)) + 8;
    pairs = (mscal ? 96u : 2u) + 32 + 30 * (uint64_t)(n - 2) + 65 + lights * (46 + 14 * (uint64_t)(n + 3)) + 8;
    vu->cycle += cycles + 64;
    vu->next_due = UINT64_MAX;
    vu->pc = ENTRY_MSCNT;
    return pairs;
}
