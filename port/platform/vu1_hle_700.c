/* VU1's program 0x700 without the interpreter ("HLE"): the same results, computed directly.

   Program 0x700 (game/data/vudata.c, D_0043D290; tools/vu1_disasm.py prints it) draws the effects:
   the sfx.dat models (fire, glows, magic). It is 210 instruction pairs at 0x700-0x7D1 and, unlike
   program 0x000, not software-pipelined: one vertex a loop pass, each result waited for. This
   computes what it computes, a vertex at a time, to the bit: the same XGKICK packets, the same data
   memory, and the same registers, flags and clock at the end, so that the interpreter can take the
   next run as if it had run this one. See vu1_hle.c (program 0x000) for the method.

   What the program does
   - MSCAL 0x700 (once per model part), 0x700-0x72C: the set-up. The vertex matrix, view (VU
     0x00-0x03) * model (0x04-0x07), into vf01-vf04 (the view matrix itself is what is stored to
     0x14-0x17; the FOGCOL packet at 0x3D is kicked after the third row); the light directions
     (0x0C-0x0F) by the model's rotation into vf05-vf07; the light colours and ambient (0x10-0x13)
     into vf08-vf11; the constants (255, 2^24, the 2048 screen offset, the 2047 guard band); vf28
     as it was is stored to 0x4D, then vf28 = 0x64, the model's colour (the vertices have none).
     Then it goes on as MSCNT does.
   - MSCNT (0x7D0: `b 0x72d`): one batch at TOP. TOP+0 is the strip's GIF tag (NLOOP = n), TOP+1+4k
     vertex k: position (int16 x3 + w, ITOF0), normal (ITOF12; its w's low 16 bits are ORed into
     XYZF2's w), ST (ITOF12, z = 1.0); the fourth qword is not read. Before the vertices, the
     texture animation: when TEX0's TBP0 in the A+D packet (TOP+0xA2.x & 0x3FFF) is VU 0x65.w, the
     qword at 0x66 replaces TEX0. Per vertex:
       c = M * pos; q = 1 / c.w; xyz = c.xyz * q; fog = fog.z + fog.w * q, clamped to 0..255;
       dots = max(L * normal, 0); colour = min((C * dots + ambient) * vf28, 255), alpha
       1 * vf28.w; ST = st * q (with 0x66's w).
     Out at TOP+0xC9+3k: ST, RGBAQ (FTOI0), XYZF2 (FTOI4, w = fog << 4 | ADC | the normal's w).
     A vertex is "clipped" when (xyz - (2048, 2048, 0)) is outside +-2047 in x or y, or z < 0; ADC
     is set when it or either of the two before it is clipped, or (from vertex 1 on) when the tag's
     w and vertex -1 (below) say so. Then the A+D packet at TOP+0xA0 is kicked, and, unless every
     vertex was clipped, the tag is copied to TOP+0xC8 and the strip kicked.
   - What reaches past the batch, all reproduced here: a "vertex -1" is transformed and clip-tested
     from the raw (unconverted) qword at the previous run's vi07 + 1; the loop's IBGTZ at 0x7BE reads
     its counter before the ISUBIU just before it (vu1.c's branch_vi), so the loop runs n + 1 times,
     and vertex n, past the strip, is computed and stored too (and counts among the clipped).

   Timing. The program's stalls are static: only the ILW at 0x731 stalls its reader 2 cycles; every
   DIV result is due by the MULQ that reads it and every CLIP by its FCAND. The cycles and pairs a
   run takes are a formula in n and the two branches (the end of vu1_hle_700_run).

   Arithmetic. Each FMAC operation is the interpreter's (v4_op), in the program's order, with its
   flags kept as the interpreter keeps them when nothing in micro memory reads them.

   Left to the interpreter: anything in flight at the start, and micro memory with an FSxxx/FMxxx
   in it. Data memory addresses wrap as the interpreter's do, and everything is done in the
   program's order, so any TOP and n are exact.

   Only the DMA controller's thread calls in here, through vu1.c's run. */
#include <stdint.h>
#include <string.h>

#include "vu1_internal.h"

#define PROGRAM_START 0x700
#define PROGRAM_PAIRS 0xD2 /* 0x700-0x7D1 */
#define PROGRAM_HASH 0xBFB6F0B4DACB1FC6ull /* FNV-1a 64 of those pairs as uploaded */
#define ENTRY_MSCAL 0x700
#define ENTRY_MSCNT 0x7D0

/* the I register's values (the program's LOIs) */
#define I_2047 0x44FFE000u
#define I_255 0x437F0000u
#define I_2_24 0x4B800000u
#define I_2048 0x45000000u
#define I_ONE 0x3F800000u

#define LANES_ALL field_lanes[15]
#define LANES_XYZ field_lanes[14]
#define LANES_XY field_lanes[12]
#define LANES_W field_lanes[1]

static int recognised;

int vu1_hle_700_recognise(const uint8_t *micro) {
    uint64_t hash = 0xCBF29CE484222325ull;
    int n;

    for (n = PROGRAM_START * 8; n < (PROGRAM_START + PROGRAM_PAIRS) * 8; n++) {
        hash = (hash ^ micro[n]) * 0x100000001B3ull;
    }
    recognised = hash == PROGRAM_HASH;
    return recognised;
}

/* ---- Arithmetic ------------------------------------------------------------------------------ */
/* The flags the run's FMAC instructions raise, as the interpreter keeps them when nothing in micro
   memory reads them (vu1.c's interpret): Z U O of them all, and the signs of their results. */
typedef struct {
    unsigned zuo;
    V4 signs;
} Flags;

/* a op b on the fields in lanes (the others are undefined), the interpreter's, with its flags */
static inline V4 fop(Flags *fl, int op, V4 a, V4 b, V4 lanes) {
    V4 r = v4_op(op, a, b, lanes, &fl->zuo);

    fl->signs |= r & lanes;
    return r;
}

/* MADDx: acc + a * b, the product rounded first (and its flags not kept) */
static inline V4 fmadd(Flags *fl, V4 acc, V4 a, V4 b, V4 lanes) {
    return fop(fl, A_ADD, acc, v4_op(A_MUL, a, b, lanes, NULL), lanes);
}

static inline V4 splat(V4 v, int f) {
    return v4_splat(v[f]);
}

/* M * v, as MULAx ACC; MADDAy; MADDAz; MADDw; *acc: ACC after it */
static inline V4 transform4(Flags *fl, const V4 *m, V4 v, V4 *acc) {
    V4 a = fop(fl, A_MUL, m[0], splat(v, 0), LANES_ALL);

    a = fmadd(fl, a, m[1], splat(v, 1), LANES_ALL);
    a = fmadd(fl, a, m[2], splat(v, 2), LANES_ALL);
    if (acc) {
        *acc = a;
    }
    return fmadd(fl, a, m[3], splat(v, 3), LANES_ALL);
}

/* a with b's lanes */
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

/* FCAND vi01, 0x3E0 on the clip flag after a vertex's two CLIPs (guard band, then 0): outside the
   guard band in x or y, or z < 0 */
static inline uint16_t clipped(uint32_t judgements) {
    return (judgements & 0x3E0u) != 0;
}

/* DIV's status bits: D for x / 0, I for 0 / 0 */
static inline uint32_t div_status(uint32_t s, uint32_t t) {
    return !vf_is_zero(t) ? 0 : vf_is_zero(s) ? 0x10u : 0x20u;
}

/* data memory as the interpreter addresses it: a VI register's value (plus an offset), wrapped */
static inline uint32_t *qword(uint8_t *data, uint32_t address) {
    return (uint32_t *)(data + (address & (DATA_QWORDS - 1)) * 16);
}

static inline V4 load(uint8_t *data, uint32_t address) {
    return v4_load(qword(data, address));
}

static inline void store(uint8_t *data, uint32_t address, V4 v) {
    v4_store(qword(data, address), v);
}

/* ---- MSCAL 0x700: the set-up (0x700-0x72C), in the program's order ----------------------------- */
static void setup(Vu1State *vu, uint8_t *data, Flags *fl) {
    V4 vf0 = v4_load(vu->vf[0]), zero, view[4], model[4], light[4], out[4];
    int n;

    /* 700-708: the constants' registers to 0 (vf00 - vf00), the view and model matrices */
    zero = fop(fl, A_SUB, vf0, vf0, LANES_ALL);
    for (n = 0; n < 4; n++) {
        view[n] = load(data, (uint32_t)n);
        model[n] = load(data, (uint32_t)(4 + n));
    }
    /* 705-718: vf01-vf04 = view * model; the view matrix to 0x14-0x17, FOGCOL kicked before the
       fourth row; vf18.w = 0 + 2047 */
    for (n = 0; n < 4; n++) {
        out[n] = transform4(fl, view, model[n], NULL);
    }
    store(data, 0x14, view[0]);
    store(data, 0x15, view[1]);
    store(data, 0x16, view[2]);
    vu->vf[18][0] = zero[0];
    vu->vf[18][1] = zero[1];
    vu->vf[18][2] = zero[2];
    vu->vf[18][3] = fop(fl, A_ADD, zero, v4_splat(I_2047), LANES_W)[3];
    vu1_xgkick(0x3D); /* 717 */
    store(data, 0x17, view[3]);
    for (n = 0; n < 4; n++) {
        v4_store(vu->vf[1 + n], out[n]);
    }
    /* 719-728: the light directions by the model's rotation, to vf05-vf07; vf28 as it was to 0x4D;
       the light colours and ambient to vf08-vf11 */
    for (n = 0; n < 4; n++) {
        light[n] = load(data, (uint32_t)(0x0C + n));
    }
    v4_store(vu->vf[5], transform4(fl, light, model[0], NULL));
    store(data, 0x4D, v4_load(vu->vf[28])); /* 720 */
    v4_store(vu->vf[6], transform4(fl, light, model[1], NULL));
    v4_store(vu->vf[7], transform4(fl, light, model[2], NULL));
    for (n = 0; n < 4; n++) {
        v4_store(vu->vf[8 + n], load(data, (uint32_t)(0x10 + n)));
    }
    v4_store(vu->vf[12], light[3]);
    /* 729-72C: the constants, and the model's colour */
    v4_store(vu->vf[19], fop(fl, A_ADD, zero, v4_splat(I_255), LANES_ALL));
    v4_store(vu->vf[17], merge(zero, fop(fl, A_ADD, zero, v4_splat(I_2_24), LANES_W), LANES_W));
    {
        V4 vf18 = v4_load(vu->vf[18]);

        v4_store(vu->vf[18], merge(vf18, fop(fl, A_ADD, vf18, v4_splat(I_2048), LANES_XY), LANES_XY));
    }
    v4_store(vu->vf[24], zero);
    v4_store(vu->vf[28], load(data, 0x64));
}

/* ---- The run ---------------------------------------------------------------------------------- */
uint64_t vu1_hle_700_run(Vu1State *vu, uint8_t *data, uint32_t pc, uint32_t top) {
    Flags fl = {0, {0, 0, 0, 0}};
    V4 m[4], l[3], c[3], ambient, offset, limit, vf24, colour_in, fog, vf29, acc, last = {0, 0, 0, 0};
    V4 pos = {0, 0, 0, 0}, nf = {0, 0, 0, 0}, sf = {0, 0, 0, 0}, proj = {0, 0, 0, 0}, rel = {0, 0, 0, 0};
    V4 scr = {0, 0, 0, 0}, colour = {0, 0, 0, 0}, dots = {0, 0, 0, 0};
    V4 vf00, raw, cj;
    uint32_t one, q = 0, q_status = 0, q_status_all, judgements = 0, judgements_prev, k, n;
    uint16_t t = (uint16_t)top, vi01, vi02 = 0, vi06, vi08 = 0, vi09 = 0, vi10, vi14, vi15 = 0;
    unsigned last_zuo = 0;
    int mscal = pc == ENTRY_MSCAL, swap, kick;
    uint64_t pairs;

    if (!recognised || (pc != ENTRY_MSCAL && pc != ENTRY_MSCNT) || vu->q_pending || vu->p_pending ||
        vu->flag_count || vu->flags_read) {
        return 0;
    }
    if (mscal) {
        setup(vu, data, &fl);
    }
    vf00 = v4_load(vu->vf[0]);
    one = vf00[3];
    for (k = 0; k < 4; k++) {
        m[k] = v4_load(vu->vf[1 + k]);
    }
    for (k = 0; k < 3; k++) {
        l[k] = v4_load(vu->vf[5 + k]);
        c[k] = v4_load(vu->vf[8 + k]);
    }
    ambient = v4_load(vu->vf[11]);
    offset = v4_load(vu->vf[18]);
    limit = v4_load(vu->vf[19]);
    vf24 = v4_load(vu->vf[24]);
    colour_in = v4_load(vu->vf[28]);

    /* 72D-735: the tag; n */
    n = (uint16_t)qword(data, t)[0] & 0x7FFFu;
    vi06 = (uint16_t)n;
    /* 72E-751: "vertex -1", the raw qword at the previous run's vi07 + 1, transformed and
       clip-tested; with the tag's w it decides ADC from vertex 1 on (the game's tags have w = 0) */
    raw = load(data, (uint16_t)(vu->vi[7] + 1));
    cj = transform4(&fl, m, raw, NULL);
    q = vf_div(one, cj[3]);
    q_status_all = div_status(one, cj[3]);
    cj = fop(&fl, A_MUL, cj, v4_splat(q), LANES_XYZ);
    rel = fop(&fl, A_SUB, cj, offset, LANES_XYZ);
    judgements = judge(rel, offset[3]) << 6 | judge(rel, vf24[3]);
    vi01 = clipped(judgements);
    vi10 = (uint16_t)qword(data, t)[3] & vi01;
    /* 755-75F: the fog; the texture animation: TEX0 replaced by 0x66 when its TBP0 is 0x65.w */
    fog = load(data, 0x3C);
    vi14 = (uint16_t)qword(data, (uint32_t)t + 0xA2)[0] & 0x3FFF;
    vi15 = (uint16_t)qword(data, 0x65)[3];
    vf29 = load(data, 0x66);
    swap = vi15 == vi14;
    if (swap) {
        store(data, (uint32_t)t + 0xA2, vf29);
    }

    /* 760-7BF: the vertices, n + 1 of them */
    acc = v4_load(vu->vf[REG_ACC]);
    for (k = 0; k <= n; k++) {
        uint32_t address = (uint16_t)(t + 1 + 4 * k), slot = (uint16_t)(t + 0xC9 + 3 * k);
        V4 c31, accw, projw, lit, st;
        uint16_t clip, adc;

        pos = v4_itof(load(data, address), 0);
        nf = v4_itof(load(data, address + 1), 12);
        sf = v4_itof(load(data, address + 2), 12);
        vi15 = (uint16_t)qword(data, address + 1)[3];
        c31 = transform4(&fl, m, pos, NULL);
        q = vf_div(one, c31[3]);
        q_status = div_status(one, c31[3]);
        q_status_all |= q_status;
        accw = fop(&fl, A_MUL, vf00, splat(fog, 2), LANES_W);
        proj = fop(&fl, A_MUL, c31, v4_splat(q), LANES_XYZ);
        st = fop(&fl, A_MUL, sf, v4_splat(q), LANES_XYZ);
        projw = fmadd(&fl, accw, fog, v4_splat(q), LANES_W);
        proj[3] = projw[3];
        rel = fop(&fl, A_SUB, proj, offset, LANES_XYZ);
        proj[3] = vf_max(vf_mini(proj[3], limit[3]), vf24[3]);
        scr = v4_ftoi(proj, 4);
        judgements_prev = judgements;
        judgements = judge(rel, offset[3]) << 6 | judge(rel, vf24[3]);
        clip = clipped(judgements);
        vi01 = clip;
        vi06 = (uint16_t)(vi06 - clip);
        adc = (uint16_t)((((uint16_t)(vi09 | clip) + 0x7FFFu) & 0x8000u) | vi15);
        vi02 = (uint16_t)((uint16_t)scr[3] | adc);
        vi09 = (uint16_t)(vi08 | clip | vi10);
        vi08 = clip;
        /* the light */
        acc = fop(&fl, A_MUL, l[0], splat(nf, 0), LANES_ALL);
        acc = fmadd(&fl, acc, l[1], splat(nf, 1), LANES_ALL);
        dots = v4_max(fmadd(&fl, acc, l[2], splat(nf, 2), LANES_ALL), vf24);
        acc = fop(&fl, A_MUL, c[0], splat(dots, 0), LANES_ALL);
        acc = fmadd(&fl, acc, c[1], splat(dots, 1), LANES_ALL);
        lit = fmadd(&fl, acc, c[2], splat(dots, 2), LANES_ALL);
        lit = merge(lit, fop(&fl, A_ADD, vf24, v4_splat(I_ONE), LANES_W), LANES_W);
        lit = merge(lit, fop(&fl, A_ADD, lit, ambient, LANES_XYZ), LANES_XYZ);
        last_zuo = 0;
        last = v4_op(A_MUL, lit, colour_in, LANES_ALL, &last_zuo);
        fl.zuo |= last_zuo;
        fl.signs |= last;
        colour = v4_ftoi(v4_mini(last, limit), 0);
        /* 7B6-7BA: XYZF2, ST (with 0x66's w), RGBAQ; XYZF2's w */
        vf29 = merge(vf29, st, LANES_XYZ);
        store(data, slot + 2, scr);
        store(data, slot, vf29);
        store(data, slot + 1, colour);
        qword(data, slot + 2)[3] = vi02;
    }

    /* 7C0-7CF: the A+D packet; the strip, unless every vertex was clipped */
    vu1_xgkick((uint16_t)(t + 0xA0));
    kick = (int16_t)vi06 > 0;
    if (kick) {
        pos = load(data, t);
        store(data, (uint32_t)t + 0xC8, pos);
        vu1_xgkick((uint16_t)(t + 0xC8));
    }

    /* the registers as the program leaves them */
    v4_store(vu->vf[12], fog);
    v4_store(vu->vf[20], colour);
    v4_store(vu->vf[21], scr);
    v4_store(vu->vf[22], dots);
    v4_store(vu->vf[25], pos);
    v4_store(vu->vf[26], nf);
    v4_store(vu->vf[27], sf);
    v4_store(vu->vf[29], vf29);
    vu->vf[30][0] = rel[0];
    vu->vf[30][1] = rel[1];
    vu->vf[30][2] = rel[2];
    v4_store(vu->vf[31], proj);
    v4_store(vu->vf[REG_ACC], acc);
    vu->q = vu->q_next = q;
    vu->q_status_next = q_status;
    vu->i = I_ONE;
    vu->vi[1] = vi01;
    vu->vi[2] = vi02;
    vu->vi[3] = 0x8000;
    vu->vi[4] = (uint16_t)(t + (kick ? 0xC8 : 0xA0));
    vu->vi[5] = t;
    vu->vi[6] = vi06;
    vu->vi[7] = (uint16_t)(t + 1 + 4 * (n + 1));
    vu->vi[8] = vi08;
    vu->vi[9] = vi09;
    vu->vi[10] = vi10;
    vu->vi[11] = 0xFFFF;
    vu->vi[12] = (uint16_t)(t + 0xC9 + 3 * (n + 1));
    vu->vi[14] = vi14;
    vu->vi[15] = vi15;
    /* the clip flag holds the last two vertices' judgements (vertex -1's when n = 0) */
    vu->clip = vu->clip_visible = (judgements_prev << 12 | judgements) & 0xFFFFFFu;
    /* the status flag: the last DIV's I and D, and the sticky bits of all of them; then the FMAC
       flags as vu1.c's interpret leaves them (the last FMAC result's, and the sticky Z S U O of all) */
    vu->status = (vu->status & ~0x30u) | q_status | (q_status_all << 6);
    {
        V4 sign = (last >> 31) & LANES_ALL;
        unsigned mac = last_zuo | (sign[0] << 7) | (sign[1] << 6) | (sign[2] << 5) | (sign[3] << 4);
        unsigned seen = mac_zsuo(fl.zuo) | (v4_any(fl.signs & SIGN) ? 2u : 0);

        vu->mac = mac;
        vu->mac_queued = (int32_t)mac;
        vu->status = (vu->status & ~0xFu) | mac_zsuo(mac) | (seen << 6);
    }
    /* The pairs: the set-up 45 (MSCNT's branch 2); 0x72D-0x75E 50, and 0x75F when TEX0 is
       replaced; 96 a vertex; the end 7, or 16 with the strip's kick. The cycles: those, and the
       ILW at 0x731's 2-cycle stall; then vu1.c's drain. */
    pairs = (mscal ? 45u : 2u) + 50 + (uint64_t)swap + 96 * (uint64_t)(n + 1) + (kick ? 16u : 7u);
    vu->cycle += pairs + 2 + 64;
    vu->next_due = UINT64_MAX;
    vu->pc = ENTRY_MSCNT;
    return pairs;
}
