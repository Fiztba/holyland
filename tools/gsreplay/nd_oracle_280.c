/* kf4-gsreplay --nd-oracle, VU1's program 0x280 (docs/port/NATIVE_DRAW_TASKS.md, P2-2): nd_oracle.c's
   oracle for the skinned models' program, both passes.

   Every run of program 0x280 (an MSCAL 0x280, or an MSCNT that goes on with it) is taken as it starts:
   the fixed block (VU1 0x00-0x73), the registers vf00-vf11 and the batch from TOP on. What the run kicks
   (--vu1 hle: port/platform/vu1_hle_280.c; interp: the interpreter) is what VU1 made: FOGCOL (an
   MSCAL's set-up), the A+D packet at TOP+0xA0, and, unless VU1 found every vertex clipped, the strip
   (TOP+0xC8) and, when the batch has a second pass, the A+D packet at TOP+0xA3 and the sphere-mapped
   strip (TOP+0x141). The same run goes through native draw (port/gs/native/gsn_nd_280.inc): the parser's
   batch from the same memory, the CPU's set-up and plan, nd_280.vert on the GPU. Compared, bit for bit:
     kicks    the packets the run kicked, against the plan's (FOGCOL, A+D, strip, A+D and strip)
     X Y Z F w R G B A S T Q   as nd_oracle.c's, for each pass's strip (the second's ST from the normal)
     ADC      the plan's (its clipping, the tag's w, the data's) against the first strip's
     plan X Y Z S T Q A   the CPU plan (what flush_batch decides by; A the corner's alpha)
     set-up   (MSCALs) both matrix sets' vertex matrices, light directions and point lights, the sphere
              matrices, the colour scale and bias, as the CPU made them, against VU1 memory after the run
   A vertex VU1's strip does not draw is counted apart ("not drawn"), as nd_oracle.c does.
   And the parser on the game's own blocks: each block VIF1 is sent (one DMA transfer) is parsed as the
   game's adapter would have it parsed (rd_mesh_from_block, RD_VF_FLOAT_SKIN); each batch of a block
   whose runs are 0x280's is compared with the batch made from VU1 memory when its run starts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/debug.h"
#include "port/gs_capture_format.h"
#include "gsn_nd280_oracle.h"
#include "nd_oracle.h"

#define RUNS_MAX 2048 /* runs a GPU batch */
#define N_MAX 36

enum {
    L_X, L_Y, L_Z, L_F, L_W, L_R, L_G, L_B, L_A, L_S, L_T, L_Q, L_ADC,
    L_PX, L_PY, L_PZ, L_PS, L_PT, L_PQ, L_PA,
    L_COUNT
};
static const char *lane_names[L_COUNT] = {"X", "Y", "Z", "F", "w", "R", "G", "B", "A", "S", "T", "Q", "ADC",
                                          "plan X", "plan Y", "plan Z", "plan S", "plan T", "plan Q", "plan A"};

typedef struct {
    uint64_t run;           /* the VU1 run (from the capture's start) */
    uint32_t packets;       /* path-1 packets the run kicked */
    int strip[2];           /* each pass's strip was kicked */
    uint32_t hle[2][N_MAX][12]; /* each pass's ST, RGBAQ, XYZF2 as kicked */
    float setup_vu[2 * 16 + 2 * 12 + 2 * 16 + 2 * 12 + 8]; /* an MSCAL's: VU1's set-up after it */
} Expect;

static struct {
    int on, limit;
    const uint8_t *vu_data;
    uint64_t runs;
    /* the run in flight */
    int pending;
    uint32_t n;
    uint32_t tag[2][4];
    /* the runs gathered for the GPU */
    GsnNd280OracleDraw *draws;
    GsnNd280OracleBatch *batches;
    Expect *expect;
    GsnNd280OracleVertex *out;
    uint32_t count;
    /* the block VIF1 was last sent */
    int block_left;
    const char *block_why;
    /* the tallies */
    uint64_t done, mscals, sphere_runs, all_clipped, lit_runs, tag_odd, vertices, vertices_drawn[2], clipped_vertices[2];
    uint64_t refused;
    uint64_t lane_differ[2][L_COUNT], lane_differ_clipped[2][L_COUNT];
    uint64_t kicks_differ, setup_compared, setup_differ[5];
    uint64_t blocks, block_batches, block_same, block_differ, runs_unparsed;
    struct {
        const char *why;
        uint64_t count;
    } unparsed_why[16];
    int shown, shown_undrawn;
} o;

static const uint32_t *qw(uint32_t address) {
    return (const uint32_t *)(o.vu_data + (size_t)(address & 0x3FF) * 16);
}

static float bits_float(uint32_t u) {
    float f;

    memcpy(&f, &u, 4);
    return f;
}

void nd280_oracle_begin(const uint8_t *vu_data, int limit) {
    o.on = 1;
    o.limit = limit;
    o.vu_data = vu_data;
    o.draws = calloc(RUNS_MAX, sizeof(*o.draws));
    o.batches = calloc(RUNS_MAX, sizeof(*o.batches));
    o.expect = calloc(RUNS_MAX, sizeof(*o.expect));
    o.out = calloc((size_t)RUNS_MAX * N_MAX * 2, sizeof(*o.out));
    if (!o.draws || !o.batches || !o.expect || !o.out) {
        fprintf(stderr, "kf4-gsreplay: --nd-oracle: out of memory\n");
        exit(1);
    }
}

static void show(uint64_t run, uint32_t k, uint32_t n, int pass, int clipped, int lane, uint32_t want, uint32_t got) {
    if (o.shown >= o.limit) {
        return;
    }
    o.shown++;
    printf("nd-oracle 0x280: run %llu vertex %u/%u pass %d%s: lane %s: VU1 %08X, native %08X", (unsigned long long)run, k,
           n, pass + 1, clipped ? " (not drawn)" : "", lane_names[lane], want, got);
    if (lane == L_S || lane == L_T || lane == L_Q || lane == L_PS || lane == L_PT || lane == L_PQ) {
        printf(" (%.9g, %.9g)", (double)bits_float(want), (double)bits_float(got));
    }
    printf("\n");
}

static void lane(uint64_t run, uint32_t k, uint32_t n, int pass, int clipped, int l, uint32_t want, uint32_t got) {
    if (want == got) {
        return;
    }
    if (clipped) {
        o.lane_differ_clipped[pass][l]++;
        if (o.shown_undrawn < 12) {
            /* (a few of these too, to say why) */
            int shown = o.shown;

            o.shown_undrawn++;
            o.shown = 0;
            show(run, k, n, pass, 1, l, want, got);
            o.shown = shown;
        }
    } else {
        o.lane_differ[pass][l]++;
        show(run, k, n, pass, 0, l, want, got);
    }
}

/* whether the strip draws vertex k (a triangle with it in is kicked without ADC), as nd_oracle.c's */
static int drawn(const uint32_t (*hle)[12], uint32_t n, uint32_t prim, uint32_t k) {
    uint32_t j;

    if (prim == 3) {
        uint32_t kick = k - k % 3 + 2;

        return kick < n && !(hle[kick][11] & 0x8000);
    }
    if (prim != 4) {
        return 1;
    }
    for (j = k; j < k + 3 && j < n; j++) {
        if (j >= 2 && !(hle[j][11] & 0x8000)) {
            return 1;
        }
    }
    return 0;
}

static void compare_setup(uint32_t i) {
    const GsnNd280OracleBatch *b = &o.batches[i];
    const float *vu = o.expect[i].setup_vu;
    static const char *names[5] = {"vertex matrices", "light directions", "point lights", "sphere matrices",
                                   "colour scale and bias"};
    const float *mine[5] = {&b->setup_vertex[0][0], &b->setup_light_dir[0][0], &b->setup_point_pos[0][0],
                            &b->setup_sphere[0][0], &b->setup_colour[0][0]};
    static const int at[5] = {0, 32, 56, 88, 112}, count[5] = {32, 24, 32, 24, 8};
    int part, j;

    o.setup_compared++;
    for (part = 0; part < 5; part++) {
        if (memcmp(mine[part], vu + at[part], (size_t)count[part] * 4) == 0) {
            continue;
        }
        o.setup_differ[part]++;
        if (o.shown < o.limit) {
            o.shown++;
            printf("nd-oracle 0x280: set-up of run %llu: %s differ:", (unsigned long long)o.expect[i].run, names[part]);
            for (j = 0; j < count[part]; j++) {
                uint32_t a, c;

                memcpy(&a, vu + at[part] + j, 4);
                memcpy(&c, mine[part] + j, 4);
                if (a != c) {
                    printf(" [%d] %08X/%08X", j, a, c);
                }
            }
            printf("\n");
        }
    }
}

/* the runs gathered: through native draw, and compared */
static void flush(void) {
    uint32_t i, k;
    int pass;

    if (!o.count) {
        return;
    }
    if (!gsn_nd280_oracle_run(o.draws, o.count, o.batches, o.out)) {
        fprintf(stderr, "kf4-gsreplay: --nd-oracle: the native GS cannot run the 0x280 oracle\n");
        exit(1);
    }
    for (i = 0; i < o.count; i++) {
        const GsnNd280OracleDraw *d = &o.draws[i];
        const GsnNd280OracleBatch *b = &o.batches[i];
        const Expect *e = &o.expect[i];
        uint32_t want_packets;

        if (!b->ok) {
            o.refused++;
            if (o.shown < o.limit) {
                o.shown++;
                printf("nd-oracle 0x280: run %llu: the parser refuses its batch: %s\n", (unsigned long long)e->run,
                       b->why ? b->why : "?");
            }
            continue;
        }
        o.done++;
        o.vertices += b->n;
        o.mscals += d->mscal != 0;
        o.sphere_runs += b->sphere != 0;
        o.all_clipped += b->all_clipped != 0;
        o.tag_odd += b->adc_tag_odd != 0;
        {
            int32_t w;

            memcpy(&w, &d->mem[0x40][3], 4);
            o.lit_runs += (int16_t)w > 0;
        }
        if (d->mscal) {
            compare_setup(i);
        }
        /* the kicks: FOGCOL (an MSCAL's), the A+D packet, then the strip, and the second pass's two */
        want_packets = (d->mscal ? 1u : 0u) + 1 + (b->all_clipped ? 0 : 1 + (b->sphere ? 2 : 0));
        if (e->packets != want_packets || e->strip[0] != !b->all_clipped || e->strip[1] != (!b->all_clipped && b->sphere)) {
            o.kicks_differ++;
            if (o.shown < o.limit) {
                o.shown++;
                printf("nd-oracle 0x280: run %llu: VU1 kicked %u packets (strips %d %d), the plan %u (all clipped %d, "
                       "second pass %d)\n",
                       (unsigned long long)e->run, e->packets, e->strip[0], e->strip[1], want_packets, b->all_clipped,
                       b->sphere);
            }
        }
        for (pass = 0; pass < 2; pass++) {
            uint32_t prim = (d->batch[pass][1] >> 15) & 7;

            if (!e->strip[pass]) {
                continue;
            }
            for (k = 0; k < b->n; k++) {
                const GsnNd280OracleVertex *v = &o.out[b->out + 2 * k + pass];
                const uint32_t *st = e->hle[pass][k], *rgbaq = st + 4, *xyzf = st + 8;
                int clipped = !drawn((const uint32_t(*)[12])e->hle[pass], b->n, prim, k);
                uint64_t run = e->run;
                uint32_t s, t, q;

                if (clipped) {
                    o.clipped_vertices[pass]++;
                } else {
                    o.vertices_drawn[pass]++;
                }
                lane(run, k, b->n, pass, clipped, L_X, xyzf[0] & 0xFFFF, v->gpu[0] & 0xFFFF);
                lane(run, k, b->n, pass, clipped, L_Y, xyzf[1] & 0xFFFF, v->gpu[1] & 0xFFFF);
                lane(run, k, b->n, pass, clipped, L_Z, (xyzf[2] >> 4) & 0xFFFFFF, v->gpu[2]);
                lane(run, k, b->n, pass, clipped, L_F, (xyzf[3] >> 4) & 0xFF, (v->gpu[3] >> 4) & 0xFF);
                lane(run, k, b->n, pass, clipped, L_W, xyzf[3] & 0x700F, v->gpu[3] & 0x700F);
                lane(run, k, b->n, pass, clipped, L_R, rgbaq[0] & 0xFF, v->gpu[4] & 0xFF);
                lane(run, k, b->n, pass, clipped, L_G, rgbaq[1] & 0xFF, (v->gpu[4] >> 8) & 0xFF);
                lane(run, k, b->n, pass, clipped, L_B, rgbaq[2] & 0xFF, (v->gpu[4] >> 16) & 0xFF);
                lane(run, k, b->n, pass, clipped, L_A, rgbaq[3] & 0xFF, v->gpu[4] >> 24);
                lane(run, k, b->n, pass, clipped, L_S, st[0], v->gpu[5]);
                lane(run, k, b->n, pass, clipped, L_T, st[1], v->gpu[6]);
                lane(run, k, b->n, pass, clipped, L_Q, st[2], v->gpu[7]);
                if (pass == 0) {
                    /* the data's ADC: the normal's w, but the last vertex's (vu1_hle_280.c) */
                    int data = k + 1 < b->n && (d->batch[3 + 4 * k][3] & 0x8000);

                    lane(run, k, b->n, pass, clipped, L_ADC, (xyzf[3] >> 15) & 1, (uint32_t)(v->plan_adc || data));
                }
                lane(run, k, b->n, pass, clipped, L_PX, xyzf[0] & 0xFFFF, (uint32_t)v->plan_xyz[0] & 0xFFFF);
                lane(run, k, b->n, pass, clipped, L_PY, xyzf[1] & 0xFFFF, (uint32_t)v->plan_xyz[1] & 0xFFFF);
                lane(run, k, b->n, pass, clipped, L_PZ, (xyzf[2] >> 4) & 0xFFFFFF, ((uint32_t)v->plan_xyz[2] >> 4) & 0xFFFFFF);
                memcpy(&s, &v->plan_stq[0], 4);
                memcpy(&t, &v->plan_stq[1], 4);
                memcpy(&q, &v->plan_stq[2], 4);
                lane(run, k, b->n, pass, clipped, L_PS, st[0], s);
                lane(run, k, b->n, pass, clipped, L_PT, st[1], t);
                lane(run, k, b->n, pass, clipped, L_PQ, st[2], q);
                lane(run, k, b->n, pass, clipped, L_PA, rgbaq[3] & 0xFF, v->plan_alpha);
            }
        }
    }
    o.count = 0;
}

/* (the run in flight is over: the next starts, or the capture ends) */
static void finish(void) {
    if (o.pending) {
        o.pending = 0;
        o.count++;
        if (o.count == RUNS_MAX) {
            flush();
        }
    }
}

void nd280_oracle_vifdata(const uint32_t *words, size_t count, uint32_t program) {
    int batches;

    if (!o.on || count % 4) {
        return;
    }
    batches = gsn_nd280_oracle_block(words, (uint32_t)(count / 4), program, &o.block_why);
    o.block_left = batches;
    if (batches > 0) {
        o.blocks++;
        o.block_batches += (uint64_t)batches;
    }
}

void nd280_oracle_vu1_run(uint32_t program, int mscal, uint32_t top) {
    GsnNd280OracleDraw *d;
    Kf4GsVu1Regs regs;
    uint32_t i, n;
    int from_block;

    if (!o.on) {
        return;
    }
    finish();
    o.runs++;
    from_block = o.block_left > 0;
    if (from_block) {
        o.block_left--;
    }
    if (program != 0x280) {
        return;
    }
    top &= 0x3FF;
    n = qw(top)[0] & 0x7FFF;
    if (n < 3 || n > N_MAX || top < 0x80 || top + 0x142 + 3 * n > 0x400) {
        return; /* (the interpreter's, not the HLE's: none in the game) */
    }
    d = &o.draws[o.count];
    memset(d, 0, sizeof(*d));
    d->mscal = (uint32_t)mscal;
    d->top = top;
    memcpy(d->mem, o.vu_data, sizeof(d->mem));
    port_vu1_debug_regs(&regs, 0);
    memcpy(d->vf, regs.vf, sizeof(d->vf));
    for (i = 0; i < GSN_ND280_ORACLE_BATCH_QWORDS; i++) {
        memcpy(d->batch[i], qw(top + i), 16);
    }
    memset(&o.expect[o.count], 0, sizeof(o.expect[o.count]));
    o.expect[o.count].run = o.runs;
    memcpy(o.tag[0], d->batch[0], 16);
    memcpy(o.tag[1], d->batch[1], 16);
    o.n = n;
    o.pending = 1;
    /* the parser on the block this run is of */
    if (from_block) {
        const char *what;
        int same = gsn_nd280_oracle_block_check(d, &what);

        if (same == 1) {
            o.block_same++;
        } else if (same == 0) {
            o.block_differ++;
            if (o.shown < o.limit) {
                o.shown++;
                printf("nd-oracle 0x280: run %llu: the block's batch differs from the run's: %s\n",
                       (unsigned long long)o.runs, what);
            }
        }
    } else {
        o.runs_unparsed++;
        for (i = 0; i < 16; i++) {
            if (!o.unparsed_why[i].why || strcmp(o.unparsed_why[i].why, o.block_why ? o.block_why : "") == 0) {
                o.unparsed_why[i].why = o.block_why ? o.block_why : "";
                o.unparsed_why[i].count++;
                break;
            }
        }
    }
}

void nd280_oracle_gif(int path, const void *data, size_t bytes) {
    Expect *e;
    const uint32_t *w = data;
    int pass;

    if (!o.pending || path != 1) {
        return;
    }
    e = &o.expect[o.count];
    e->packets++;
    if (o.draws[o.count].mscal) {
        /* VU1's set-up as it is by now (it is done before any packet but FOGCOL's, and the passes leave it) */
        static const uint32_t from[10] = {0x14, 0x1C, 0x18, 0x20, 0x4A, 0x53, 0x2C, 0x30, 0x72, 0x73};
        static const uint32_t qwords[10] = {4, 4, 3, 3, 4, 4, 3, 3, 1, 1};
        float *to = e->setup_vu;
        int j;

        for (j = 0; j < 10; j++) {
            memcpy(to, qw(from[j]), qwords[j] * 16);
            to += qwords[j] * 4;
        }
    }
    if (bytes != 16 + 48 * (size_t)o.n) {
        return;
    }
    for (pass = 0; pass < 2; pass++) {
        if (!e->strip[pass] && (pass == 0 || e->strip[0]) && memcmp(w, o.tag[pass], 16) == 0) {
            e->strip[pass] = 1;
            memcpy(e->hle[pass], w + 4, 48 * (size_t)o.n);
            return;
        }
    }
}

void nd280_oracle_end(void) {
    uint64_t total[2] = {0, 0}, total_clipped[2] = {0, 0};
    int l, pass;
    uint32_t i;

    if (!o.on) {
        return;
    }
    finish();
    flush();
    printf("nd-oracle 0x280: %llu runs (%llu MSCALs), %llu vertices; %llu with a second pass, %llu with point "
           "lights, %llu all clipped (no strip), %llu with the tag's w odd; %llu runs' batches refused by the parser\n",
           (unsigned long long)o.done, (unsigned long long)o.mscals, (unsigned long long)o.vertices,
           (unsigned long long)o.sphere_runs, (unsigned long long)o.lit_runs, (unsigned long long)o.all_clipped,
           (unsigned long long)o.tag_odd, (unsigned long long)o.refused);
    for (pass = 0; pass < 2; pass++) {
        printf("nd-oracle 0x280: pass %d: %llu vertices drawn, %llu not; lanes differing (drawn/not drawn):", pass + 1,
               (unsigned long long)o.vertices_drawn[pass], (unsigned long long)o.clipped_vertices[pass]);
        for (l = 0; l < L_COUNT; l++) {
            if (pass == 1 && l == L_ADC) {
                continue;
            }
            printf(" %s %llu/%llu", lane_names[l], (unsigned long long)o.lane_differ[pass][l],
                   (unsigned long long)o.lane_differ_clipped[pass][l]);
            total[pass] += o.lane_differ[pass][l];
            total_clipped[pass] += o.lane_differ_clipped[pass][l];
        }
        printf("\n");
    }
    printf("nd-oracle 0x280: kicks differing in %llu runs; set-up: %llu MSCALs compared; differing: vertex matrices "
           "%llu, light directions %llu, point lights %llu, sphere matrices %llu, colour scale and bias %llu\n",
           (unsigned long long)o.kicks_differ, (unsigned long long)o.setup_compared,
           (unsigned long long)o.setup_differ[0], (unsigned long long)o.setup_differ[1],
           (unsigned long long)o.setup_differ[2], (unsigned long long)o.setup_differ[3],
           (unsigned long long)o.setup_differ[4]);
    printf("nd-oracle 0x280: parser on the game's blocks: %llu blocks parsed (%llu batches); runs: %llu the same as "
           "their block's batch, %llu differ, %llu not from a parsed block",
           (unsigned long long)o.blocks, (unsigned long long)o.block_batches, (unsigned long long)o.block_same,
           (unsigned long long)o.block_differ, (unsigned long long)o.runs_unparsed);
    for (i = 0; i < 16 && o.unparsed_why[i].why; i++) {
        printf("%s %llu: \"%s\"", i ? "," : " (the last block's", (unsigned long long)o.unparsed_why[i].count,
               o.unparsed_why[i].why);
    }
    printf("%s\n", o.unparsed_why[0].why ? ")" : "");
    printf("nd-oracle 0x280: %llu lane differences (pass 1 %llu, pass 2 %llu; %llu more on vertices not drawn)\n",
           (unsigned long long)(total[0] + total[1]), (unsigned long long)total[0], (unsigned long long)total[1],
           (unsigned long long)(total_clipped[0] + total_clipped[1]));
}
