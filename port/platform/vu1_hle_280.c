/* VU1's program 0x280 without the interpreter: the same results, computed directly (as vu1_hle.c does
   for 0x000, whose top comment says how and why; this follows it).

   Program 0x280 (game/data/vudata.c, vu1_float_vertex_program; tools/vu1_disasm.py prints it) draws
   the skinned models: enemies, the player's arm and held weapon, and the CPU-cut float triangles.
   It is 556 instruction pairs at 0x280-0x4AB.

   What the program does
   - MSCAL 0x280: the set-up, done twice, once per matrix set. Set B: the model matrix at VU
     0x08-0x0B times the view (0x00-0x03) to 0x1C-0x1F, the light directions (0x0C-0x0F) by its
     rotation to 0x20-0x22, the point lights (0x41-0x44, count in 0x40.w) into its space at
     0x53-0x56 (colours to 0x57-0x5A, the count to 0x52); the FOGCOL packet at 0x3D is kicked. Set A:
     the same from the model matrix at 0x04-0x07 to 0x14-0x17, 0x18-0x1A and 0x49-0x51. The two
     sphere-map matrices (0x24-0x26, 0x28-0x2A) are copied to 0x2C-0x2E and 0x30-0x32, the colour scale
     and bias (0x64, 0x65) to 0x72 and 0x73. Then it goes on as MSCNT does.
   - MSCNT (0x4AA: `b 0x350`): one batch at TOP. TOP+0 is the strip's GIF tag (NLOOP = n vertices),
     TOP+2+4k vertex k: position, normal (its w's low 16 bits ORed into XYZF2's w, but the last
     vertex's), ST (its w's low 16 bits choose the matrix set: 0 A, else B: two-matrix rigid
     skinning), colour, all floats. Per vertex, as program 0x000: c = M * pos, q = 1 / c.w,
     xyz = c.xyz * q, fog, dots = max(L * normal, 0), ST = st * q; the colour is
     min((C * dots + ambient) * u, 255) where u's w is the vertex's alpha * scale.w + bias.w, and u's
     xyz carry from vertex to vertex: vertex 0's colour * scale + bias, then the last u * scale + bias
     (a pipeline that loads only the alpha of the vertices after the first). Clipping and ADC as
     0x000's, and if every vertex and vertex 0 again are clipped (the count reaches n) the run ends
     after the A+D kick: no light, no strip. The point lights add as 0x000's do, but with the light's
     position in the vertex's own set (vertex 0 takes the tag's w as its set, vertex -1 set A), the
     attenuation is set A's, and a pass covers vertices -1 .. n-1. The strip at TOP+0xC8 is kicked.
   - The second pass, when TOP+0xA4.z's low 16 bits are not 0: the A+D packet at TOP+0xA3 is kicked,
     and a strip of the same XYZF2s (copied from the first's output) with ST = (sphere-map matrix of
     the vertex's set * normal + 1) / 2, Q 1, and RGBA (255, 255, 255, vertex 1's alpha) is built at
     TOP+0x141 (tag from TOP+1) and kicked.

   Timing. As in 0x000, only Q, P and the clip flag are read without a stall, and every DIV, ESADD
   and CLIP result is due by its reader, the FCAND at 0x384 included (it sees both of vertex -1's
   CLIPs). The run's stalls are fixed: 2 cycles before 0x3D5 and 2 before 0x3FF. The pairs a run takes
   depend on the paths: the matrix switches, the light chosen per vertex, and the sign of each
   second-pass vertex's FTOI12 z (an IBLEZ around two identical stores).

   Not handled here, left to the interpreter: n < 3 or n > 36, a batch not between 0x80 and the end
   of data memory, anything in flight at the start, micro memory with an FSxxx/FMxxx. The game's
   batches have n = 3..30 at TOP 0x80 or 0x240.

   Only the DMA controller's thread calls in here, through vu1.c's run. */
#include <stdint.h>
#include <string.h>

#include "vu1_internal.h"
#include "vu1_hle_ops.h"

#define PROGRAM_FIRST 0x280
#define PROGRAM_PAIRS 0x22C /* 0x280-0x4AB */
#define PROGRAM_HASH 0xBE120E495123DF8Aull /* FNV-1a 64 of those pairs as uploaded */
#define ENTRY_MSCNT 0x4AA
#define N_MIN 3
#define N_MAX 36

/* the I register's values (the program's LOIs) */
#define I_2047 0x44FFE000u
#define I_255 0x437F0000u
#define I_2_24 0x4B800000u
#define I_2048 0x45000000u
#define I_ONE 0x3F800000u
#define I_HALF 0x3F000000u

#define LANES_XY field_lanes[12]
#define LANES_Z field_lanes[2]

int vu1_hle_280_recognise(const uint8_t *micro) {
    return hle_micro_hash(micro, PROGRAM_FIRST, PROGRAM_PAIRS) == PROGRAM_HASH;
}

/* ---- MSCAL 0x280: the set-up (0x280-0x34F), in the program's order ------------------------------ */
/* One matrix set's point lights (0x2A5-0x2DB, 0x302-0x338): vf10-vf12 the model rotation's rows
   negated as the program does, vf20-vf23 the lights less the model's translation (vf13), each
   transformed to vf25-vf28 (x, y and z each a MULAx; MADDAy; MADDz of their own, w = 1 * w); the
   stores in between are the caller's. */
static void setup_lights(Flags *fl, V4 *vf, uint8_t *data, uint32_t count_to, uint32_t lights_to) {
    V4 acc, p;
    int n;

    for (n = 0; n < 4; n++) {
        vf[10 + n] = fop(fl, 1, A_ADD, vf[24], vf[5 + n], LANES_ALL);
    }
    for (n = 0; n < 4; n++) {
        vf[20 + n] = load(data, (uint32_t)(0x41 + n));
    }
    store(data, count_to, vf[1]); /* the light count, and what else 0x40 holds */
    vf[10] = merge(vf[10], fop(fl, 1, A_SUB, vf[24], vf[10], field_lanes[6]), field_lanes[6]);
    vf[11] = merge(vf[11], fop(fl, 1, A_SUB, vf[24], vf[11], field_lanes[10]), field_lanes[10]);
    vf[12] = merge(vf[12], fop(fl, 1, A_SUB, vf[24], vf[12], field_lanes[12]), field_lanes[12]);
    for (n = 0; n < 4; n++) {
        vf[20 + n] = merge(vf[20 + n], fop(fl, 1, A_SUB, vf[20 + n], vf[13], LANES_XYZ), LANES_XYZ);
    }
    for (n = 0; n < 4; n++) {
        p = vf[20 + n];
        acc = fop(fl, 1, A_MUL, vf[10], splat(p, 0), LANES_XYZ);
        acc = fmadd(fl, 1, acc, vf[11], splat(p, 1), LANES_XYZ);
        vf[25 + n] = merge(vf[25 + n], fmadd(fl, 1, acc, vf[12], splat(p, 2), LANES_XYZ), LANES_XYZ);
        vf[25 + n] = merge(vf[25 + n], fop(fl, 1, A_MUL, vf[0], splat(p, 3), LANES_W), LANES_W);
    }
    store(data, lights_to, vf[25]);
    store(data, lights_to + 1, vf[26]);
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)(0x45 + n));
    }
    for (n = 0; n < 4; n++) {
        store(data, lights_to + 4 + (uint32_t)n, vf[1 + n]); /* their colours */
    }
    store(data, lights_to + 2, vf[27]);
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)(0x0C + n));
    }
}

static void setup(Vu1State *vu, uint8_t *data, Flags *fl) {
    V4 vf[32], acc, zero, m[4];
    int n;

    for (n = 0; n < 32; n++) {
        vf[n] = v4_load(vu->vf[n]);
    }
    /* 280-28B: the sphere-map matrices copied */
    vf[1] = load(data, 0x24);
    vf[4] = load(data, 0x28);
    vf[2] = load(data, 0x25);
    vf[5] = load(data, 0x29);
    vf[3] = load(data, 0x26);
    vf[6] = load(data, 0x2A);
    store(data, 0x2C, vf[1]);
    store(data, 0x30, vf[4]);
    store(data, 0x2D, vf[2]);
    store(data, 0x31, vf[5]);
    store(data, 0x2E, vf[3]);
    store(data, 0x32, vf[6]);
    /* 28C-2A4: set B's vertex matrix, view * the model matrix at 0x08, to 0x1C-0x1F; the FOGCOL
       packet is kicked after the third row is stored and before the fourth */
    zero = fop(fl, 1, A_SUB, vf[0], vf[0], LANES_ALL);
    vf[19] = vf[18] = vf[17] = vf[24] = zero;
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)n);
        vf[5 + n] = load(data, (uint32_t)(0x08 + n));
        m[n] = vf[1 + n];
    }
    for (n = 0; n < 4; n++) {
        vf[9 + n] = transform4(fl, 1, m, vf[5 + n]);
    }
    store(data, 0x1C, vf[9]);
    store(data, 0x1D, vf[10]);
    vu->vi[14] = 0x3D;
    vf[1] = load(data, 0x40); /* 2A1 */
    vf[18] = merge(vf[18], fop(fl, 1, A_ADD, vf[18], v4_splat(I_2047), LANES_W), LANES_W); /* 2A2 */
    store(data, 0x1E, vf[11]);
    vu1_xgkick(0x3D); /* 2A3 */
    store(data, 0x1F, vf[12]);
    /* 2A5-2D7: set B's point lights to 0x53-0x56, colours 0x57-0x5A, count 0x52 */
    setup_lights(fl, vf, data, 0x52, 0x53);
    store(data, 0x56, vf[28]); /* 2DB */
    /* 2D8-2E7: set B's light directions to 0x20-0x22; the constants */
    for (n = 0; n < 4; n++) {
        m[n] = vf[1 + n];
    }
    vf[5] = transform4(fl, 1, m, vf[5]);
    store(data, 0x20, vf[5]);
    vf[6] = transform4(fl, 1, m, vf[6]);
    store(data, 0x21, vf[6]);
    vf[7] = transform4(fl, 1, m, vf[7]);
    vf[19] = fop(fl, 1, A_ADD, vf[19], v4_splat(I_255), LANES_ALL);
    vf[17] = merge(vf[17], fop(fl, 1, A_ADD, vf[17], v4_splat(I_2_24), LANES_W), LANES_W);
    vf[18] = merge(vf[18], fop(fl, 1, A_ADD, vf[18], v4_splat(I_2048), LANES_XY), LANES_XY);
    vf[11] = load(data, 0x10);
    store(data, 0x22, vf[7]);
    /* 2E8-300: set A's vertex matrix, view * the model matrix at 0x04, to 0x14-0x17 */
    vf[24] = fop(fl, 1, A_SUB, vf[0], vf[0], LANES_ALL);
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)n);
        vf[5 + n] = load(data, (uint32_t)(0x04 + n));
        m[n] = vf[1 + n];
    }
    for (n = 0; n < 4; n++) {
        vf[9 + n] = transform4(fl, 1, m, vf[5 + n]);
    }
    store(data, 0x14, vf[9]);
    store(data, 0x15, vf[10]);
    store(data, 0x16, vf[11]);
    vf[1] = load(data, 0x40); /* 2FD */
    store(data, 0x17, vf[12]);
    /* 302-334: set A's point lights to 0x4A-0x4D, colours 0x4E-0x51, count 0x49 */
    setup_lights(fl, vf, data, 0x49, 0x4A);
    store(data, 0x4D, vf[28]); /* 338 */
    /* 335-347: set A's light directions to 0x18-0x1A (the last one's MULAx; MADDAy; MADDAz leave
       the accumulator); the vertex matrix, light colours and ambient back */
    for (n = 0; n < 4; n++) {
        m[n] = vf[1 + n];
    }
    vf[5] = transform4(fl, 1, m, vf[5]);
    store(data, 0x18, vf[5]);
    vf[6] = transform4(fl, 1, m, vf[6]);
    acc = fop(fl, 1, A_MUL, m[0], splat(vf[7], 0), LANES_ALL);
    acc = fmadd(fl, 1, acc, m[1], splat(vf[7], 1), LANES_ALL);
    acc = fmadd(fl, 1, acc, m[2], splat(vf[7], 2), LANES_ALL);
    vf[7] = fmadd(fl, 1, acc, m[3], splat(vf[7], 3), LANES_ALL);
    store(data, 0x19, vf[6]);
    for (n = 0; n < 4; n++) {
        vf[1 + n] = load(data, (uint32_t)(0x14 + n));
        vf[8 + n] = load(data, (uint32_t)(0x10 + n));
    }
    store(data, 0x1A, vf[7]);
    /* 348-34D: the colour scale and bias */
    vf[1] = load(data, 0x64);
    vf[2] = load(data, 0x65);
    store(data, 0x72, vf[1]);
    store(data, 0x73, vf[2]);
    for (n = 1; n < 32; n++) {
        v4_store(vu->vf[n], vf[n]);
    }
    v4_store(vu->vf[REG_ACC], acc);
    vu->i = I_2048;
    vu->vi[1] = 0x18;
    vu->vi[3] = 0x14;
    vu->vi[4] = 0x40;
}

/* ---- MSCNT: a batch --------------------------------------------------------------------------- */
typedef struct {
    V4 m[2][4];   /* the vertex matrices: set A (0x14-0x17), set B (0x1C-0x1F) */
    V4 l[2][3];   /* the light directions: A (0x18-0x1A), B (0x20-0x22) */
    V4 c[3];      /* vf08-vf10: the light colours */
    V4 ambient;   /* vf11 */
    V4 fog;       /* vf12: VU 0x3C */
    V4 scale;     /* vf15: VU 0x72 */
    V4 bias;      /* vf16: VU 0x73 */
    V4 offset;    /* vf18: (2048, 2048, 0, the guard band 2047) */
    V4 limit;     /* vf19: 255 */
    V4 floor;     /* vf24: 0 */
    uint32_t one; /* vf00.w */
    int fast;     /* the fast way may be tried (vu1_hle.c's top) */
#if VU_DAZ
    unsigned csr; /* MXCSR in the run (toward zero, DAZ) */
#endif
} Batch;

typedef struct {
    V4 c, nf, sf, dots, acc, proj, rel, st, scr, colour, last, u;
    unsigned last_zuo;
    uint32_t q, q_status, judgements;
} Vertex;

/* the low 16 bits of a word, as ILW loads them */
static inline uint16_t vi_word(uint8_t *data, uint32_t address, int field) {
    return (uint16_t)qword(data, address)[field];
}

/* Vertex k (its four qwords at TOP+2+4k), u_prev the last vertex's u (k > 0). 0 when the fast way
   cannot be trusted with it. */
static ALWAYS_INLINE int vertex_as(Flags *fl, int exact, const Batch *b, uint8_t *data, uint32_t top, uint32_t k,
                                   V4 u_prev, Vertex *v) {
    uint32_t address = top + 2 + 4 * k;
    V4 pos = load(data, address), colour = load(data, address + 3), acc, lit, fogw, proj;
    int set = vi_word(data, address + 2, 3) != 0;

    v->nf = load(data, address + 1);
    v->sf = load(data, address + 2);
    if (!exact && (has_huge(pos) || has_huge(v->nf) || has_huge(v->sf) || has_huge(colour) || has_huge(u_prev))) {
        return 0;
    }
    v->c = transform4(fl, exact, b->m[set], pos);
    acc = fop(fl, exact, A_MUL, b->l[set][0], splat(v->nf, 0), LANES_ALL);
    acc = fmadd(fl, exact, acc, b->l[set][1], splat(v->nf, 1), LANES_ALL);
    v->dots = v4_max(fmadd(fl, exact, acc, b->l[set][2], splat(v->nf, 2), LANES_ALL), b->floor);
    v->q = vf_div(b->one, v->c[3]);
    v->q_status = div_status(b->one, v->c[3]);
    if (!exact && (v->q & 0x7F800000u) == 0x7F800000u) {
        return 0;
    }
    /* u: vertex 0's colour, or the last u with this vertex's alpha, * scale + bias */
    v->u = fop(fl, exact, A_MUL, k ? merge(u_prev, colour, LANES_W) : colour, b->scale, LANES_ALL);
    v->u = fop(fl, exact, A_ADD, v->u, b->bias, LANES_ALL);
    acc = fop(fl, exact, A_MUL, b->c[0], splat(v->dots, 0), LANES_ALL);
    acc = fmadd(fl, exact, acc, b->c[1], splat(v->dots, 1), LANES_ALL);
    lit = fmadd(fl, exact, acc, b->c[2], splat(v->dots, 2), LANES_ALL);
    lit = merge(lit, fop(fl, exact, A_ADD, b->floor, v4_splat(I_ONE), LANES_W), LANES_W);
    fogw = fop(fl, exact, A_MUL, v4_splat(b->one), splat(b->fog, 2), LANES_W);
    v->acc = merge(acc, fogw, LANES_W);
    proj = fop(fl, exact, A_MUL, v->c, v4_splat(v->q), LANES_XYZ);
    v->st = fop(fl, exact, A_MUL, v->sf, v4_splat(v->q), LANES_XYZ);
    proj = merge(proj, fmadd(fl, exact, fogw, b->fog, v4_splat(v->q), LANES_W), LANES_W);
    lit = merge(lit, fop(fl, exact, A_ADD, lit, b->ambient, LANES_XYZ), LANES_XYZ);
    v->rel = fop(fl, exact, A_SUB, proj, b->offset, LANES_XYZ);
    proj[3] = vf_max(vf_mini(proj[3], b->limit[3]), b->floor[3]);
    v->proj = proj;
    v->last_zuo = 0;
    v->last = v4_op(A_MUL, lit, v->u, LANES_ALL, &v->last_zuo);
    if (exact) {
        fl->zuo |= v->last_zuo;
        fl->signs |= v->last;
    }
    v->colour = v4_ftoi(v4_mini(v->last, b->limit), 0);
    v->scr = v4_ftoi(proj, 4);
    v->judgements = judge(v->rel, b->offset[3]) << 6 | judge(v->rel, b->floor[3]);
    return 1;
}

static void vertex(Flags *fl, const Batch *b, uint8_t *data, uint32_t top, uint32_t k, V4 u_prev, Vertex *v) {
#if VU_DAZ
    if (b->fast) {
        int done;

        _mm_setcsr((b->csr & ~MXCSR_FLAGS) | MXCSR_FTZ);
        done = vertex_as(fl, 0, b, data, top, k, u_prev, v);
        done &= (_mm_getcsr() & MXCSR_TROUBLE) == 0;
        _mm_setcsr(b->csr);
        if (done) {
            return;
        }
    }
#endif
    vertex_as(fl, 1, b, data, top, k, u_prev, v);
}

/* ---- The point lights (0x40C-0x453) ------------------------------------------------------------ */
typedef struct {
    V4 vf16, vf20, vf21, vf22, vf23, vf25, vf26, vf27, vf29, vf30, a, bpos, colour;
    uint32_t p;
    uint16_t vi15;
    V4 last;
    unsigned last_zuo;
} LightEnd;

typedef struct {
    V4 a, bpos, colour; /* vf13 (set A's position; its w attenuates), vf07 (set B's), vf14 */
    uint32_t w23;       /* vf23.w: min(vf22.w, 0) */
    int fast;
} Light;

typedef struct {
    V4 add, att, a22, a23, sum;
    unsigned sum_zuo;
} LightVertex;

static ALWAYS_INLINE int light_vertex_as(Flags *fl, int exact, const Batch *b, const Light *l, V4 raw, V4 pos,
                                         int set, LightVertex *lv) {
    V4 d;
    uint32_t p;

    if (!exact && has_huge(pos)) {
        return 0;
    }
    d = fop(fl, exact, A_SUB, set ? l->bpos : l->a, pos, LANES_XYZ);
    p = sum_squares(exact, d);
    lv->add = fop(fl, exact, A_ADD, b->floor, v4_itof(raw, 0), LANES_ALL);
    lv->att = fop(fl, exact, A_MUL, v4_splat(p), l->a, LANES_W);
    lv->att = fop(fl, exact, A_SUB, v4_splat(b->one), lv->att, LANES_W);
    lv->a22 = fop(fl, exact, A_MUL, l->colour, splat(lv->att, 3), LANES_XYZ);
    lv->a23 = v4_max(lv->a22, b->floor);
    lv->a23[3] = l->w23;
    lv->sum_zuo = 0;
    lv->sum = v4_op(A_ADD, lv->add, lv->a23, LANES_ALL, &lv->sum_zuo);
    if (exact) {
        fl->zuo |= lv->sum_zuo;
        fl->signs |= lv->sum;
    }
    return 1;
}

static void light_vertex(Flags *fl, const Batch *b, const Light *l, V4 raw, V4 pos, int set, LightVertex *lv) {
#if VU_DAZ
    if (l->fast) {
        int done;

        _mm_setcsr((b->csr & ~MXCSR_FLAGS) | MXCSR_FTZ);
        done = light_vertex_as(fl, 0, b, l, raw, pos, set, lv);
        done &= (_mm_getcsr() & MXCSR_TROUBLE) == 0;
        _mm_setcsr(b->csr);
        if (done) {
            return;
        }
    }
#endif
    light_vertex_as(fl, 1, b, l, raw, pos, set, lv);
}

/* the set of vertex k for the light passes: its ST's w, but vertex 0 takes the tag's w (the pass's
   pipeline loads one vertex late) and vertex -1 set A */
static int light_set(uint8_t *data, uint32_t top, int32_t k) {
    if (k < 0) {
        return 0;
    }
    return vi_word(data, k ? top + 4 + 4 * (uint32_t)k : top, 3) != 0;
}

/* One light's pass (0x40F-0x452) over vertices -1 .. n-1, and the stores of slots -3 and -2 from
   the registers as the pass found them. Returns its pairs. */
static uint64_t light_pass(Flags *fl, const Batch *b, uint8_t *data, uint32_t top, uint32_t n, uint32_t index,
                           LightEnd *e) {
    Light l;
    LightVertex lv;
    V4 add, att, a22, a23, sum, d, pos;
    int32_t k;
    uint64_t pairs = 15 + 25 + 3;

    l.bpos = load(data, 0x53 + index);
    l.a = load(data, 0x4A + index);
    l.colour = load(data, 0x4E + index);
    l.w23 = vf_mini(e->vf22[3], b->floor[3]);
    l.fast = b->fast && !has_huge(l.a) && !has_huge(l.bpos) && !has_huge(l.colour) &&
             (l.w23 & 0x7F800000u) != 0x7F800000u;
    store(data, top + 0xC1, v4_ftoi(v4_mini(e->vf29, b->limit), 0));
    add = fop(fl, 1, A_ADD, b->floor, e->vf27, LANES_ALL);
    att = fop(fl, 1, A_SUB, v4_splat(b->one), e->vf21, LANES_W);
    a22 = fop(fl, 1, A_MUL, l.colour, splat(att, 3), LANES_XYZ);
    a23 = v4_max(a22, b->floor);
    a23[3] = l.w23;
    sum = fop(fl, 1, A_ADD, add, a23, LANES_ALL);
    store(data, top + 0xC4, v4_ftoi(v4_mini(sum, b->limit), 0));
    for (k = -1; k < (int32_t)n; k++) {
        uint32_t slot = top + 0xCA + 3 * (uint32_t)k;
        int set = light_set(data, top, k);

        light_vertex(fl, b, &l, load(data, slot), load(data, top + 2 + 4 * (uint32_t)k), set, &lv);
        store(data, slot, v4_ftoi(v4_mini(lv.sum, b->limit), 0));
        if (k >= 0) {
            pairs += 18 + (set ? 3 : 1); /* iteration k, which chose by the same vi15 */
        }
    }
    /* iteration n: vertex n's distance (the last ESADD); the end: vertex n+1 subtracted */
    {
        int set_n = vi_word(data, top + 4 + 4 * n, 3) != 0, set_n1 = vi_word(data, top + 8 + 4 * n, 3) != 0;

        pos = load(data, top + 2 + 4 * n);
        d = fop(fl, 1, A_SUB, set_n ? l.bpos : l.a, pos, LANES_XYZ);
        e->p = sum_squares(1, d);
        pairs += 18 + (set_n ? 3 : 1) + (set_n1 ? 6 : 5);
        pos = load(data, top + 6 + 4 * n);
        e->vf20 = merge(e->vf20, fop(fl, 1, A_SUB, set_n1 ? l.bpos : l.a, pos, LANES_XYZ), LANES_XYZ);
        e->vf25 = pos;
        e->vi15 = vi_word(data, top + 8 + 4 * n, 3);
    }
    e->vf26 = load(data, top + 0xCA + 3 * n);
    e->vf27 = v4_itof(e->vf26, 0);
    e->vf16 = lv.add;
    e->vf21 = merge(e->vf21, lv.att, LANES_W);
    e->vf22 = merge(e->vf22, lv.a22, LANES_XYZ);
    e->vf23 = lv.a23;
    e->vf29 = lv.sum;
    e->vf30 = v4_ftoi(v4_mini(lv.sum, b->limit), 0);
    e->last = lv.sum;
    e->last_zuo = lv.sum_zuo;
    e->a = l.a;
    e->bpos = l.bpos;
    e->colour = l.colour;
    return pairs;
}

/* ---- The second pass (0x45C-0x4A7): sphere-mapped ST ---------------------------------------------- */
typedef struct {
    V4 uv;           /* vf22: ((Ms * normal).xy + 1) * 0.5, z 1, w 0 */
    V4 z23;          /* vf23.z: FTOI12 of (Ms * normal).z */
    V4 acc;
    unsigned uv_zuo; /* the MULI.xy's flags (the run's last) */
} Sphere;

static void sphere_vertex(Flags *fl, const V4 *ms, V4 normal, V4 ones, V4 vf22, V4 vf23, Sphere *s) {
    V4 acc = fop(fl, 1, A_MUL, ms[0], splat(normal, 0), LANES_ALL), uv, z;

    acc = fmadd(fl, 1, acc, ms[1], splat(normal, 1), LANES_ALL);
    acc = fmadd(fl, 1, acc, ms[2], splat(normal, 2), LANES_ALL);
    s->acc = acc;
    uv = fmadd(fl, 1, acc, ones, ones, LANES_XY);
    z = fmadd(fl, 1, acc, v4_splat(0), v4_splat(0), LANES_Z);
    s->uv_zuo = 0;
    uv = v4_op(A_MUL, uv, v4_splat(I_HALF), LANES_XY, &s->uv_zuo);
    fl->zuo |= s->uv_zuo;
    fl->signs |= uv & LANES_XY;
    s->uv = merge(vf22, uv, LANES_XY);
    s->z23 = merge(vf23, v4_ftoi(z, 12), LANES_Z);
}

/* ---- The run ---------------------------------------------------------------------------------- */
uint64_t vu1_hle_280_run(Vu1State *vu, uint8_t *data, uint32_t pc, uint32_t top) {
    Flags fl = {0, {0, 0, 0, 0}};
    Batch b;
    Vertex v[2];
    Vertex *cur = &v[0], *prev = &v[1];
    V4 vf20_in, vf29_in, vf30_in, mac_value, mac_lanes = LANES_ALL, u_prev = {0, 0, 0, 0};
    uint32_t n, k, q_status_all, adc_tag, count, lights_word, lights, i;
    unsigned mac_zuo;
    int clip1, clip2, mscal = pc == PROGRAM_FIRST, all_clipped;
    uint16_t set_flag, tag_w, vi02 = 0, vi09 = 0, vi15 = 0, nw;
    uint64_t pairs, stalls;
    LightEnd e;

    if ((pc != PROGRAM_FIRST && pc != ENTRY_MSCNT) || vu->q_pending || vu->p_pending || vu->flag_count ||
        vu->flags_read) {
        return 0;
    }
    top &= DATA_QWORDS - 1;
    n = qword(data, top)[0] & 0x7FFF;
    if (n < N_MIN || n > N_MAX || top < 0x80 || top + 0x142 + 3 * n > DATA_QWORDS) {
        return 0;
    }
    pairs = mscal ? 0xD0 : 2;
    if (mscal) {
        setup(vu, data, &fl);
    }

    /* 350-385: the batch's start */
    for (i = 0; i < 4; i++) {
        b.m[0][i] = load(data, 0x14 + i);
        b.m[1][i] = load(data, 0x1C + i);
    }
    for (i = 0; i < 3; i++) {
        b.l[0][i] = load(data, 0x18 + i);
        b.l[1][i] = load(data, 0x20 + i);
        b.c[i] = v4_load(vu->vf[8 + i]);
    }
    b.ambient = v4_load(vu->vf[11]);
    b.fog = load(data, 0x3C);
    b.scale = load(data, 0x72);
    b.bias = load(data, 0x73);
    b.offset = v4_load(vu->vf[18]);
    b.limit = v4_load(vu->vf[19]);
    b.floor = v4_load(vu->vf[24]);
    b.one = vu->vf[0][3];
    b.fast = 0;
#if VU_DAZ
    b.csr = _mm_getcsr();
    b.fast = (vu->status & 0xC0u) == 0xC0u && !has_huge(v4_splat(b.one)) && !has_huge(b.c[0]) &&
             !has_huge(b.c[1]) && !has_huge(b.c[2]) && !has_huge(b.ambient) && !has_huge(b.fog) &&
             !has_huge(b.scale) && !has_huge(b.bias) && !has_huge(b.offset) && !has_huge(b.limit) &&
             !has_huge(b.floor);
    for (i = 0; i < 4 && b.fast; i++) {
        b.fast = !has_huge(b.m[0][i]) && !has_huge(b.m[1][i]) && (i == 3 || (!has_huge(b.l[0][i]) && !has_huge(b.l[1][i])));
    }
#endif
    vf20_in = v4_load(vu->vf[20]);
    vf29_in = v4_load(vu->vf[29]);
    vf30_in = v4_load(vu->vf[30]);
    tag_w = vi_word(data, top, 3);
    set_flag = vi_word(data, top + 4, 3); /* vi14: the set loaded */
    pairs += 8 + (set_flag ? 9 : 8) + 30;

    vertex(&fl, &b, data, top, 0, u_prev, cur);
    /* "Vertex -1": vertex 0's own projection clip-tested before the loop (the same judgements); the
       FCAND at 0x384 sees both CLIPs, and with the tag's w decides whether every vertex gets ADC */
    clip1 = clipped(cur->judgements);
    adc_tag = tag_w & (uint16_t)clip1;
    clip2 = 0;
    count = (uint32_t)clip1;
    q_status_all = 0;
    store(data, top + 0xC6, vf29_in);
    store(data, top + 0xC7, vf20_in);

    for (k = 0;; k++) {
        int clip = clipped(cur->judgements);
        uint16_t flag;

        q_status_all |= cur->q_status;
        count += (uint32_t)clip;
        if (k == n - 1) {
            break;
        }
        nw = vi_word(data, top + 3 + 4 * k, 3);
        vi09 = (uint16_t)((((uint32_t)(clip | clip1 | clip2) | adc_tag) + 0x7FFF) & 0x8000) | nw;
        vi02 = (uint16_t)((uint16_t)cur->scr[3] | vi09);
        vi15 = nw;
        {
            V4 st = cur->st, xyz = cur->scr;
            uint32_t slot = top + 0xC9 + 3 * k;

            st[3] = vf29_in[3];
            xyz[3] = vi02;
            store(data, slot, st);
            store(data, slot + 1, cur->colour);
            store(data, slot + 2, xyz);
        }
        clip2 = clip1;
        clip1 = clip;
        /* the next vertex's set: a matrix switch when it differs from the one loaded */
        flag = vi_word(data, top + 8 + 4 * k, 3);
        pairs += (k < n - 2 ? 30 : 2 + 54) + (flag == set_flag ? 0 : flag ? 10 : 9);
        set_flag = flag;
        u_prev = cur->u;
        {
            Vertex *t = prev;

            prev = cur;
            cur = t;
        }
        vertex(&fl, &b, data, top, k + 1, u_prev, cur);
    }
    mac_value = cur->last;
    mac_zuo = cur->last_zuo;

    /* 0F0-3FD: the tag, the A+D kick, the last vertex's ST; and the end if all are clipped */
    store(data, top + 0xC8, load(data, top));
    vu1_xgkick(top + 0xA0);
    {
        V4 st = cur->st;

        st[3] = vf29_in[3];
        store(data, top + 0xC9 + 3 * (n - 1), st);
    }
    all_clipped = (int16_t)(uint16_t)(count - n) >= 0;
    vu->vi[6] = (uint16_t)(count - n);
    {
        uint32_t adc_src = (uint32_t)(clipped(cur->judgements) | clip1 | clip2) | adc_tag;

        if (all_clipped) {
            vi09 = (uint16_t)(adc_src + 0x7FFF);
        } else {
            V4 xyz = cur->scr;

            vi09 = (uint16_t)((adc_src + 0x7FFF) & 0x8000);
            vi02 = (uint16_t)((uint16_t)cur->scr[3] | vi09);
            xyz[3] = vi02;
            store(data, top + 0xCA + 3 * (n - 1), cur->colour);
            store(data, top + 0xCB + 3 * (n - 1), xyz);
        }
    }

    /* the registers the main pass leaves */
    {
        V4 vf20 = all_clipped ? v4_mini(cur->last, b.limit) : cur->colour, vf29 = cur->st, vf30 = cur->rel;

        vf29[3] = vf29_in[3];
        vf30[3] = vf30_in[3];
        for (i = 0; i < 4; i++) {
            v4_store(vu->vf[1 + i], b.m[set_flag != 0][i]);
        }
        for (i = 0; i < 3; i++) {
            v4_store(vu->vf[5 + i], b.l[set_flag != 0][i]);
        }
        v4_store(vu->vf[12], b.fog);
        v4_store(vu->vf[15], b.scale);
        v4_store(vu->vf[16], b.bias);
        v4_store(vu->vf[20], vf20);
        v4_store(vu->vf[21], cur->scr);
        v4_store(vu->vf[22], cur->dots);
        v4_store(vu->vf[25], load(data, top));
        v4_store(vu->vf[26], cur->nf);
        v4_store(vu->vf[27], cur->sf);
        v4_store(vu->vf[28], cur->u);
        v4_store(vu->vf[29], vf29);
        v4_store(vu->vf[30], vf30);
        v4_store(vu->vf[31], cur->proj);
        v4_store(vu->vf[REG_ACC], cur->acc);
    }
    vu->q = vu->q_next = cur->q;
    vu->q_status_next = cur->q_status;
    vu->i = I_ONE;
    vu->vi[1] = (uint16_t)clipped(cur->judgements);
    vu->vi[2] = vi02;
    vu->vi[3] = 0x8000;
    vu->vi[4] = (uint16_t)(top + 0xC8);
    vu->vi[5] = (uint16_t)top;
    vu->vi[7] = (uint16_t)(top + 4 * n + 2);
    vu->vi[8] = (uint16_t)clip1;
    vu->vi[9] = vi09;
    vu->vi[10] = adc_tag;
    vu->vi[11] = 0;
    vu->vi[12] = (uint16_t)(top + 0xC6 + 3 * n);
    vu->vi[13] = (uint16_t)n;
    vu->vi[14] = set_flag;
    vu->vi[15] = vi15;
    vu->clip = vu->clip_visible = (prev->judgements << 12 | cur->judgements) & 0xFFFFFFu;
    stalls = 2;
    pairs += 2;

    if (!all_clipped) {
        stalls += 2;
        pairs += 17 - 2;
        /* 408-453: the point lights */
        lights_word = qword(data, 0x49)[3];
        lights = (int16_t)lights_word > 0 ? (uint32_t)(int16_t)lights_word : 0;
        memset(&e, 0, sizeof(e));
        e.vf16 = b.bias;
        e.vf20 = cur->colour;
        e.vf21 = cur->scr;
        e.vf22 = cur->dots;
        e.vf23 = v4_load(vu->vf[23]);
        e.vf25 = load(data, top);
        e.vf26 = cur->nf;
        e.vf27 = cur->sf;
        e.vf29 = v4_load(vu->vf[29]);
        e.vf30 = v4_load(vu->vf[30]);
        for (i = 0; i < lights; i++) {
            pairs += light_pass(&fl, &b, data, top, n, i, &e);
            mac_value = e.last;
            mac_zuo = e.last_zuo;
        }
        if (lights) {
            v4_store(vu->vf[7], e.bpos);
            v4_store(vu->vf[13], e.a);
            v4_store(vu->vf[14], e.colour);
            vu->p = vu->p_next = e.p;
            vu->vi[12] = 0xFFFF;
            vu->vi[15] = e.vi15;
        }
        v4_store(vu->vf[16], e.vf16);
        v4_store(vu->vf[20], e.vf20);
        v4_store(vu->vf[21], e.vf21);
        v4_store(vu->vf[22], e.vf22);
        v4_store(vu->vf[23], e.vf23);
        v4_store(vu->vf[25], e.vf25);
        v4_store(vu->vf[26], e.vf26);
        v4_store(vu->vf[27], e.vf27);
        v4_store(vu->vf[29], e.vf29);
        v4_store(vu->vf[30], e.vf30);
        vu->vi[1] = (uint16_t)(0x4A + lights);
        vu->vi[2] = (uint16_t)(0x4E + lights);
        vu->vi[3] = (uint16_t)(top + 0xC7);
        vu->vi[11] = (uint16_t)(lights ? 0 : lights_word);
        vu->vi[14] = (uint16_t)(0x53 + lights);
        vu1_xgkick(top + 0xC8); /* 454 */

        /* 456-45B, whether or not the second pass follows */
        vu->vi[2] = vi_word(data, top + 0xA4, 2);
        vu->vi[6] = (uint16_t)(top + 2);
        vu->vi[7] = (uint16_t)(top + 0xCB);
        vu->vi[8] = (uint16_t)(top + 0x142);
        vu->vf[16][3] = qword(data, top + 0xCD)[3];
        pairs += 9;
        if (vu->vi[2] == 0) {
            pairs += 2;
        } else {
            /* 45C-4A7: the second pass */
            V4 ms[2][3], ones, vf14, vf16, vf22, vf23, xyz = {0, 0, 0, 0}, normal;
            Sphere s;
            uint32_t alpha = vu->vf[16][3];

            vu->vi[4] = (uint16_t)(top + 0xA3);
            vf16 = merge(v4_load(vu->vf[16]), fop(&fl, 1, A_ADD, v4_load(vu->vf[0]), b.limit, LANES_XYZ), LANES_XYZ);
            vu1_xgkick(top + 0xA3);
            vf14 = merge(v4_load(vu->vf[14]), fop(&fl, 1, A_ADD, v4_load(vu->vf[0]), b.floor, LANES_XYZ), LANES_XYZ);
            vf14[3] = alpha;
            for (i = 0; i < 3; i++) {
                ms[0][i] = load(data, 0x2C + i);
                ms[1][i] = load(data, 0x30 + i);
            }
            vf14 = merge(vf14, v4_ftoi(vf14, 0), LANES_XYZ);
            vf16 = merge(vf16, v4_ftoi(vf16, 0), LANES_XYZ);
            ones = fop(&fl, 1, A_ADD, b.floor, v4_splat(I_ONE), LANES_ALL);
            vf22 = v4_load(vu->vf[22]);
            vf22 = merge(vf22, fop(&fl, 1, A_ADD, v4_load(vu->vf[0]), v4_splat(I_ONE), LANES_Z), LANES_Z);
            vf22 = merge(vf22, fop(&fl, 1, A_SUB, v4_load(vu->vf[0]), v4_load(vu->vf[0]), LANES_W), LANES_W);
            vf23 = v4_load(vu->vf[23]);
            pairs += 23 + 5 + 5 + 8 + 6;
            for (k = 0; k < n; k++) {
                int set = vi_word(data, top + 4 + 4 * k, 3) != 0;
                uint32_t out = top + 0x142 + 3 * k;

                normal = load(data, top + 3 + 4 * k);
                xyz = load(data, top + 0xCB + 3 * k);
                sphere_vertex(&fl, ms[set], normal, ones, vf22, vf23, &s);
                vf22 = s.uv;
                vf23 = s.z23;
                store(data, out, vf22);
                store(data, out + 2, xyz);
                store(data, out + 1, vf16);
                pairs += (k < n - 1 ? 15 : 0) + ((int16_t)(uint16_t)s.z23[2] <= 0 ? 1 : 2);
            }
            store(data, top + 0x141, load(data, top + 1));
            vu1_xgkick(top + 0x141);
            mac_value = s.uv;
            mac_lanes = LANES_XY;
            mac_zuo = s.uv_zuo;
            for (i = 0; i < 3; i++) {
                v4_store(vu->vf[1 + i], ms[0][i]);
                v4_store(vu->vf[5 + i], ms[1][i]);
            }
            v4_store(vu->vf[14], vf14);
            v4_store(vu->vf[16], vf16);
            v4_store(vu->vf[22], vf22);
            v4_store(vu->vf[23], vf23);
            v4_store(vu->vf[25], xyz);
            v4_store(vu->vf[26], load(data, top + 3 + 4 * n));
            v4_store(vu->vf[27], load(data, top + 1));
            v4_store(vu->vf[29], ones);
            v4_store(vu->vf[REG_ACC], s.acc);
            vu->i = I_HALF;
            vu->vi[1] = (uint16_t)s.z23[2];
            vu->vi[2] = 0x80;
            vu->vi[4] = (uint16_t)(top + 0x141);
            vu->vi[6] = (uint16_t)(top + 2 + 4 * n);
            vu->vi[7] = (uint16_t)(top + 0xCB + 3 * n);
            vu->vi[8] = (uint16_t)(top + 0x142 + 3 * (n - 1));
            vu->vi[11] = 0;
            vu->vi[14] = (uint16_t)(1 - vi_word(data, top + 4 + 4 * n, 3));
            vu->vi[15] = 1;
        }
    }

    /* the status and MAC flags (vu1_hle.c's run says how) */
    vu->status = (vu->status & ~0x30u) | cur->q_status | (q_status_all << 6);
    {
        V4 sign = (mac_value >> 31) & mac_lanes;
        unsigned mac = mac_zuo | (sign[0] << 7) | (sign[1] << 6) | (sign[2] << 5) | (sign[3] << 4);
        unsigned seen = mac_zsuo(fl.zuo | mac_zuo) | (v4_any((fl.signs | (mac_value & mac_lanes)) & SIGN) ? 2u : 0);

        vu->mac = mac;
        vu->mac_queued = (int32_t)mac;
        vu->status = (vu->status & ~0xFu) | mac_zsuo(mac) | (seen << 6);
    }
    vu->cycle += pairs + stalls + 64;
    vu->next_due = UINT64_MAX;
    vu->pc = ENTRY_MSCNT;
    return pairs;
}
