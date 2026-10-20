/* VU1's program 0x5A0 without the interpreter ("HLE"): the same results, computed directly.

   Program 0x5A0 (game/data/vudata.c, vu1_posed_model_program; tools/vu1_disasm.py prints it) draws
   the translucent constant-colour silhouettes of pool entities (drawn with DATE and FBA). It is 162
   instruction pairs at 0x5A0-0x641, software-pipelined, like program 0x000 (vu1_hle.c, whose
   approach this follows: read its comment first). This computes what it computes, a vertex at a
   time, to the bit: the same XGKICK packets, data memory, registers, flags and clock at the end.

   What the program does
   - MSCAL 0x5A0 (once per model part): the set-up. It multiplies the view matrix (VU 0x00-0x03,
     into vf09-vf12) by the model matrix (0x04-0x07, into vf05-vf08) into the vertex matrix (vf01-
     vf04; unlike 0x000 it is not stored), sets the constants (vf18 = (2048, 2048, 0, 2047), vf17.w
     = 2^24, vf19 = vf24 = 0) and the colour, vf20 = FTOI0(vf24) = 0 (the 128s ADDI puts in it first
     are overwritten), and kicks the FOGCOL packet at 0x3D. Then it goes on as MSCNT does.
   - MSCNT (0x640: `b 0x5BF`): one batch at TOP. TOP+0 is the strip's GIF tag (NLOOP = n vertices),
     TOP+1+4k vertex k: position (ITOF0), normal (ITOF12; only its w is used: the low 16 bits are
     ORed into XYZF2's w), ST (ITOF12, z = 1.0 but for the last two vertices, which keep theirs) and
     a fourth qword nothing reads. Per vertex:
       c = M * pos; q = 1 / c.w; xyz = c.xyz * q; fog = fog.z + fog.w * q, clamped to 0..vf19.w (0);
       ST = st * q.
     Out at TOP+0xC9+3k: ST (w: vf29.w as the run found it), RGBAQ (vf20, the colour), XYZF2 (FTOI4,
     w = fog << 4 | ADC | the normal's w). Clipping and ADC are 0x000's: a vertex is clipped when
     (xyz - (2048, 2048, 0)) is outside +-2047 in x or y, or z < 0, and ADC is set when it or either
     of the two before it is clipped, or when the tag's w and vertex -1's guard band -z say so. The
     strip's tag is copied to TOP+0xC8 and the strip is kicked from there.
   - Leftovers that are visible: the light dots of the first and last normals (vf22) are taken and
     multiplied into ACC by vf08 and vf09, for nothing; vf28 is ITOF0'd twice without being loaded;
     "vertex -1" (vertex 0 scaled by 1 / the previous run's vf31.w) is clip tested, and vf29 and the
     colour are stored for it at TOP+0xC6/0xC7. All of it is reproduced here.

   Timing. As in 0x000 the stalls are static (two of them, at 0x5C3 and 0x636) and every DIV is due
   by the MULQ that reads it, every CLIP by its FCAND, except the FCAND at 0x5DD, which sees only
   vertex -1's guard-band CLIP. The cycles and pairs a run takes are a formula in n (the end of
   vu1_hle_5a0_run).

   Arithmetic is the interpreter's (v4_op), in the program's order, with its flags kept as the
   interpreter keeps them; see vu1_hle.c.

   Not handled here, left to the interpreter: n < 3 (the loop would run 65536 times), n > 49 (the
   output would reach the input), a TOP whose batch wraps around data memory or reaches into the
   fixed block, anything in flight at the start, and micro memory with an FSxxx/FMxxx in it. The
   game's batches have n = 3..30 at TOP 0x80 or 0x240.

   Only the DMA controller's thread calls in here, through vu1.c's run (hle_try). */
#include <stdint.h>
#include <string.h>

#include "vu1_internal.h"

#define PROGRAM_FIRST 0x5A0
#define PROGRAM_PAIRS 0xA2                /* 0x5A0-0x641 */
#define PROGRAM_HASH 0x6EF951435EB3493Cull /* FNV-1a 64 of those pairs as uploaded */
#define ENTRY_MSCNT 0x640
#define N_MIN 3
#define N_MAX 49

/* the I register's values (the program's LOIs) */
#define I_2047 0x44FFE000u
#define I_128 0x43000000u
#define I_2_24 0x4B800000u
#define I_2048 0x45000000u
#define I_ONE 0x3F800000u

#define LANES_ALL field_lanes[15]
#define LANES_XYZ field_lanes[14]
#define LANES_XY field_lanes[12]
#define LANES_Z field_lanes[2]
#define LANES_W field_lanes[1]

static int recognised;

int vu1_hle_5a0_recognise(const uint8_t *micro) {
    uint64_t hash = 0xCBF29CE484222325ull;
    int n;

    for (n = 0; n < PROGRAM_PAIRS * 8; n++) {
        hash = (hash ^ micro[PROGRAM_FIRST * 8 + n]) * 0x100000001B3ull;
    }
    recognised = hash == PROGRAM_HASH;
    return recognised;
}

/* ---- Arithmetic (as vu1_hle.c's exact form) ---------------------------------------------------- */
typedef struct {
    unsigned zuo;
    V4 signs;
} Flags;

static inline V4 fop(Flags *fl, int op, V4 a, V4 b, V4 lanes) {
    V4 r = v4_op(op, a, b, lanes, &fl->zuo);

    fl->signs |= r & lanes;
    return r;
}

/* MADDx: acc + a * b, the product rounded first (and its flags not kept) */
static inline V4 fmadd(Flags *fl, V4 acc, V4 a, V4 b, V4 lanes) {
    return fop(fl, A_ADD, acc, v4_op(A_MUL, a, b, lanes, NULL), lanes);
}

/* M * v, as MULAx ACC; MADDAy; MADDAz; MADDw */
static inline V4 transform4(Flags *fl, const V4 *m, V4 v) {
    V4 acc = fop(fl, A_MUL, m[0], v4_splat(v[0]), LANES_ALL);

    acc = fmadd(fl, acc, m[1], v4_splat(v[1]), LANES_ALL);
    acc = fmadd(fl, acc, m[2], v4_splat(v[2]), LANES_ALL);
    return fmadd(fl, acc, m[3], v4_splat(v[3]), LANES_ALL);
}

/* L * v.xyz, as MULAx ACC; MADDAy; MADDz */
static inline V4 transform3(Flags *fl, const V4 *m, V4 v) {
    V4 acc = fop(fl, A_MUL, m[0], v4_splat(v[0]), LANES_ALL);

    acc = fmadd(fl, acc, m[1], v4_splat(v[1]), LANES_ALL);
    return fmadd(fl, acc, m[2], v4_splat(v[2]), LANES_ALL);
}

static inline V4 merge(V4 a, V4 b, V4 lanes) {
    return (a & ~lanes) | (b & lanes);
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

/* FCAND vi01, 0x3E0 after a vertex's two CLIPs (guard band, then 0) */
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

/* ---- MSCAL 0x5A0: the set-up (0x5A0-0x5BE) ----------------------------------------------------- */
static void setup(Vu1State *vu, uint8_t *data, Flags *fl) {
    V4 vf[32], zero;
    int n;

    for (n = 0; n < 32; n++) {
        vf[n] = v4_load(vu->vf[n]);
    }
    /* 5A0-5A7: the constants' registers to 0 (vf00 - vf00); the view matrix to vf09-vf12, the model
       matrix to vf05-vf08 */
    zero = fop(fl, A_SUB, vf[0], vf[0], LANES_ALL);
    vf[19] = vf[18] = vf[17] = vf[24] = zero;
    for (n = 0; n < 4; n++) {
        vf[9 + n] = load(data, (uint32_t)n);
        vf[5 + n] = load(data, (uint32_t)(4 + n));
    }
    /* 5A5-5B4: the vertex matrix, view * model */
    for (n = 0; n < 4; n++) {
        vf[1 + n] = transform4(fl, &vf[9], vf[5 + n]);
    }
    /* 5B5-5BE: the guard band, the FOGCOL packet, the colour and the screen offset */
    vf[18] = merge(vf[18], fop(fl, A_ADD, vf[18], v4_splat(I_2047), LANES_W), LANES_W);
    vu->vi[14] = 0x3D;
    vu1_xgkick(0x3D); /* 5B7 */
    vf[20] = merge(vf[20], fop(fl, A_ADD, vf[24], v4_splat(I_128), LANES_XYZ), LANES_XYZ);
    vf[20] = merge(vf[20], fop(fl, A_ADD, vf[24], v4_splat(I_128), LANES_W), LANES_W);
    vf[17] = merge(vf[17], fop(fl, A_ADD, vf[17], v4_splat(I_2_24), LANES_W), LANES_W);
    vf[18] = merge(vf[18], fop(fl, A_ADD, vf[18], v4_splat(I_2048), LANES_XY), LANES_XY);
    vf[20] = v4_ftoi(vf[24], 0);
    for (n = 1; n < 32; n++) {
        v4_store(vu->vf[n], vf[n]);
    }
}

/* ---- MSCNT: a batch --------------------------------------------------------------------------- */
typedef struct {
    V4 c;       /* M * position (vf31 after the transform) */
    V4 st;      /* ST (ITOF12; z = 1.0 but for the last two) */
    V4 proj;    /* vf31 at the end: xyz * q, fog clamped */
    V4 rel;     /* vf30.xyz: proj - offset */
    V4 stq;     /* vf29.xyz: ST * q */
    V4 scr;     /* XYZF2 (FTOI4 of proj) */
    V4 fogw;    /* ACC.w: 1 * fog.z */
    uint32_t q, q_status;
    uint32_t judgements; /* guard band << 6 | zero */
} Vertex;

/* vertex k's projection, from its transform (v->c) and ST (v->st); the last op, ST * q, is the
   caller's (its flags may be the run's last) */
static void project(Flags *fl, Vertex *v, V4 fog, V4 offset, V4 limit, V4 zero, uint32_t one) {
    V4 proj;

    v->q = vf_div(one, v->c[3]);
    v->q_status = div_status(one, v->c[3]);
    v->fogw = fop(fl, A_MUL, v4_splat(one), v4_splat(fog[2]), LANES_W);
    proj = fop(fl, A_MUL, v->c, v4_splat(v->q), LANES_XYZ);
    proj = merge(proj, fmadd(fl, v->fogw, fog, v4_splat(v->q), LANES_W), LANES_W);
    v->rel = fop(fl, A_SUB, proj, offset, LANES_XYZ);
    proj[3] = vf_max(vf_mini(proj[3], limit[3]), zero[3]);
    v->proj = proj;
    v->scr = v4_ftoi(proj, 4);
    v->judgements = judge(v->rel, offset[3]) << 6 | judge(v->rel, zero[3]);
}

uint64_t vu1_hle_5a0_run(Vu1State *vu, uint8_t *data, uint32_t pc, uint32_t top) {
    Flags fl = {0, {0, 0, 0, 0}};
    V4 m[4], l[3], d[2], fog, offset, limit, zero, colour, vf0;
    V4 vf28_in, vf29_in, vf30_in, vf31_in, pos, dots0, junk, mac_value = {0, 0, 0, 0};
    V4 nrm = {0, 0, 0, 0}, dotsn = {0, 0, 0, 0}, acc = {0, 0, 0, 0};
    Vertex v;
    uint32_t n, k, judgements_junk, judgements_prev = 0, q_junk, q_status_all, adc_tag, count;
    uint32_t c1, c2, tag_w;
    uint16_t last_bits = 0, adc_bits = 0;
    unsigned mac_zuo = 0;
    int mscal = pc == PROGRAM_FIRST;
    uint64_t cycles, pairs;

    if (!recognised || (pc != PROGRAM_FIRST && pc != ENTRY_MSCNT) || vu->q_pending || vu->p_pending ||
        vu->flag_count || vu->flags_read) {
        return 0;
    }
    top &= DATA_QWORDS - 1;
    n = qword(data, top)[0] & 0x7FFF;
    if (n < N_MIN || n > N_MAX || top < 0x80 || top + 0xC9 + 3 * n > DATA_QWORDS) {
        return 0;
    }
    if (mscal) {
        setup(vu, data, &fl);
    }
    for (k = 0; k < 4; k++) {
        m[k] = v4_load(vu->vf[1 + k]);
    }
    for (k = 0; k < 3; k++) {
        l[k] = v4_load(vu->vf[5 + k]);
    }
    d[0] = v4_load(vu->vf[8]);
    d[1] = v4_load(vu->vf[9]);
    offset = v4_load(vu->vf[18]);
    limit = v4_load(vu->vf[19]);
    zero = v4_load(vu->vf[24]);
    colour = v4_load(vu->vf[20]);
    vf0 = v4_load(vu->vf[0]);
    vf28_in = v4_load(vu->vf[28]);
    vf29_in = v4_load(vu->vf[29]);
    vf30_in = v4_load(vu->vf[30]);
    vf31_in = v4_load(vu->vf[31]);
    fog = load(data, 0x3C);
    tag_w = (uint16_t)qword(data, top)[3];

    /* 5BF-5DE: vertex 0's transform, the dots of its normal, and "vertex -1": the first DIV divides
       by the vf31.w the last run left, and vertex 0's transform scaled by it is clip-tested. The
       FCAND at 0x5DD sees only its guard-band CLIP's -z; with the tag's w that decides whether
       every vertex gets ADC. */
    v.c = transform4(&fl, m, v4_itof(load(data, top + 1), 0));
    q_junk = vf_div(vf0[3], vf31_in[3]);
    q_status_all = div_status(vf0[3], vf31_in[3]);
    dots0 = transform3(&fl, l, v4_itof(load(data, top + 2), 12));
    junk = fop(&fl, A_MUL, v.c, v4_splat(q_junk), LANES_XYZ);
    dots0 = v4_max(dots0, zero);
    junk = fop(&fl, A_SUB, junk, offset, LANES_XYZ);
    judgements_junk = judge(junk, offset[3]) << 6 | judge(junk, zero[3]);
    adc_tag = tag_w & ((judgements_junk & (0x20u << 6)) != 0);
    /* the first loop pass stores vf29 and the colour for vertex -1 (its XYZF2 goes to TOP+0xC8,
       where the tag replaces it before anything reads it) */
    store(data, top + 0xC6, vf29_in);
    store(data, top + 0xC7, colour);

    /* 5DF-5FC a pass, then 5FD-63D: the vertices; each one's ADC is its clip and the two before */
    c1 = (uint32_t)clipped(judgements_junk);
    c2 = 0;
    count = c1;
    for (k = 0; k < n; k++) {
        uint32_t slot = top + 0xC9 + 3 * k, c;
        uint16_t bits;
        V4 xyz;

        v.st = v4_itof(load(data, top + 3 + 4 * k), 12);
        if (k + 2 < n) {
            v.st = merge(v.st, fop(&fl, A_ADD, vf0, v4_splat(I_ONE), LANES_Z), LANES_Z);
        }
        project(&fl, &v, fog, offset, limit, zero, vf0[3]);
        q_status_all |= v.q_status;
        if (k == n - 2) {
            /* 5FE-603: vertex 0's dots by vf08 and vf09 into ACC; the last normal's dots */
            acc = fop(&fl, A_MUL, d[0], v4_splat(dots0[0]), LANES_ALL);
            acc = fmadd(&fl, acc, d[1], v4_splat(dots0[1]), LANES_ALL);
            nrm = v4_itof(load(data, top + 2 + 4 * (n - 1)), 12);
            dotsn = transform3(&fl, l, nrm);
        }
        /* ST * q: for the last vertex, the run's last FMAC instruction */
        mac_zuo = 0;
        v.stq = v4_op(A_MUL, v.st, v4_splat(v.q), LANES_XYZ, &mac_zuo);
        mac_value = v.stq;
        if (k != n - 1) {
            fl.zuo |= mac_zuo;
            fl.signs |= v.stq & LANES_XYZ;
        }
        c = (uint32_t)clipped(v.judgements);
        count += c;
        adc_bits = (uint16_t)((((c | c1 | c2 | adc_tag) + 0x7FFF) & 0x8000) | (uint16_t)qword(data, top + 2 + 4 * k)[3]);
        bits = (uint16_t)(adc_bits | (uint16_t)v.scr[3]);
        xyz = v.scr;
        xyz[3] = bits;
        {
            V4 st = v.stq;

            st[3] = vf29_in[3];
            store(data, slot, st);
        }
        store(data, slot + 1, colour);
        store(data, slot + 2, xyz);
        last_bits = bits;
        c2 = c1;
        c1 = c;
        if (k + 1 < n) {
            judgements_prev = v.judgements;
            pos = v4_itof(load(data, top + 1 + 4 * (k + 1)), 0);
            v.c = transform4(&fl, m, pos);
        }
        if (k == n - 2) {
            /* 617-61F: the last normal's dots, clamped, by vf08 and vf09 into ACC */
            dotsn = v4_max(dotsn, zero);
            acc = fop(&fl, A_MUL, d[0], v4_splat(dotsn[0]), LANES_ALL);
            acc = fmadd(&fl, acc, d[1], v4_splat(dotsn[1]), LANES_ALL);
        }
    }
    /* 61C-626: the tag to TOP+0xC8; 63C: the strip is kicked */
    store(data, top + 0xC8, load(data, top));
    vu1_xgkick(top + 0xC8);

    /* the registers as the program leaves them */
    v4_store(vu->vf[12], fog);
    v4_store(vu->vf[21], v.scr);
    v4_store(vu->vf[22], dotsn);
    v4_store(vu->vf[25], load(data, top));
    v4_store(vu->vf[26], nrm);
    v4_store(vu->vf[27], v.st);
    v4_store(vu->vf[28], v4_itof(v4_itof(vf28_in, 0), 0));
    v4_store(vu->vf[29], merge(vf29_in, v.stq, LANES_XYZ));
    v4_store(vu->vf[30], merge(vf30_in, v.rel, LANES_XYZ));
    v4_store(vu->vf[31], v.proj);
    v4_store(vu->vf[REG_ACC], merge(acc, v.fogw, LANES_W));
    vu->q = vu->q_next = v.q;
    vu->q_status_next = v.q_status;
    vu->i = I_ONE;
    vu->vi[1] = (uint16_t)c1;
    vu->vi[2] = last_bits;
    vu->vi[3] = 0x8000;
    vu->vi[4] = (uint16_t)(top + 0xC8);
    vu->vi[5] = (uint16_t)top;
    vu->vi[6] = (uint16_t)(count - (n + 2)); /* never >= 0, so the IBGEZ at 0x632 is never taken */
    vu->vi[7] = (uint16_t)(top + 4 * n + 1);
    vu->vi[8] = (uint16_t)c2;
    vu->vi[9] = adc_bits;
    vu->vi[10] = (uint16_t)adc_tag;
    vu->vi[11] = 0;
    vu->vi[12] = (uint16_t)(top + 0xC6 + 3 * n);
    vu->vi[13] = (uint16_t)(n + 2);
    vu->vi[15] = (uint16_t)qword(data, top + 2 + 4 * (n - 1))[3];
    /* the clip flag holds the last two vertices' judgements */
    vu->clip = vu->clip_visible = (judgements_prev << 12 | v.judgements) & 0xFFFFFFu;
    /* the status flag: the last DIV's I and D, and the sticky bits of all of them; then the FMAC
       flags as vu1.c's interpret leaves them (the last FMAC result's, and the sticky Z S U O of all) */
    vu->status = (vu->status & ~0x30u) | v.q_status | (q_status_all << 6);
    {
        V4 sign = (mac_value >> 31) & LANES_XYZ;
        unsigned mac = mac_zuo | (sign[0] << 7) | (sign[1] << 6) | (sign[2] << 5) | (sign[3] << 4);
        unsigned seen = mac_zsuo(fl.zuo | mac_zuo) | (v4_any((fl.signs | (mac_value & LANES_XYZ)) & SIGN) ? 2u : 0);

        vu->mac = mac;
        vu->mac_queued = (int32_t)mac;
        vu->status = (vu->status & ~0xFu) | mac_zsuo(mac) | (seen << 6);
    }
    /* The clock: the set-up is 31 cycles (MSCNT's branch 2); 0x5BF-0x5DE 33; each loop pass (n - 2)
       30; 0x5FD-0x63F 68; then vu1.c's drain. The pairs run: 31 or 2; 32; 30 a pass; 67. */
    cycles = (mscal ? 31u : 2u) + 33 + 30 * (uint64_t)(n - 2) + 68;
    pairs = (mscal ? 31u : 2u) + 32 + 30 * (uint64_t)(n - 2) + 67;
    vu->cycle += cycles + 64;
    vu->next_due = UINT64_MAX;
    vu->pc = ENTRY_MSCNT;
    return pairs;
}
