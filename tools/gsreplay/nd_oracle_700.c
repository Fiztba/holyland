/* kf4-gsreplay --nd-oracle, VU1's program 0x700 (docs/port/NATIVE_DRAW_TASKS.md, P4-1): nd_oracle.c's
   oracle for the effects' program.

   Every run of program 0x700 (an MSCAL 0x700, or an MSCNT that goes on with it) is taken as it starts: VU1
   0x00-0x66, the registers and the batch from TOP on. What the run kicks (--vu1 hle:
   port/platform/vu1_hle_700.c; interp: the interpreter) is what VU1 made: FOGCOL (an MSCAL's set-up), the A+D
   packet at TOP+0xA0 (its TEX0 perhaps replaced by VU1 0x66, the texture animation), and, unless n of the
   n + 1 vertices it tests were clipped, the strip (TOP+0xC8). The same run goes through native draw
   (port/gs/native/gsn_nd_700.inc): the parser's batch from the same memory, the CPU's set-up, A+D writes and
   plan, nd_700.vert on the GPU. Compared, bit for bit:
     kicks    the packets the run kicked (FOGCOL, A+D, strip) against the plan's
     A+D      the A+D packet's writes against the plan's (the TEX0 swap)
     X Y Z F w R G B A S T Q   as nd_oracle.c's
     ADC      the plan's (its clipping, the data's) against the strip's
     plan X Y Z S T Q A   the CPU plan (what flush_batch decides by; A the corner's alpha)
     set-up   (MSCALs) the vertex matrix (vf01-vf04) and the light directions (vf05-vf07) as the CPU made
              them, against VU1's after the run
   A vertex VU1's strip does not draw is counted apart ("not drawn"), as nd_oracle.c does.
   And the parser on the game's own blocks: each block VIF1 is sent (one DMA transfer) is parsed as the game's
   adapter would have it parsed (rd_mesh_from_block, RD_VF_PACKED16); each batch of a block whose runs are
   0x700's is compared with the batch made from VU1 memory when its run starts, and whether the block itself
   knows vertex n (the qword past the strip that decides the kick when one vertex is left unclipped). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/debug.h"
#include "port/gs_capture_format.h"
#include "gsn_nd700_oracle.h"
#include "nd_oracle.h"

#define RUNS_MAX 2048 /* runs a GPU batch */
#define N_MAX 39
#define PACKETS_MAX 4

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
    uint32_t sizes[PACKETS_MAX];
    int strip;              /* the strip was kicked */
    uint32_t ad[17][4];     /* the A+D packet as kicked (the one before the strip, or the last) */
    uint32_t ad_qwords;
    uint32_t hle[N_MAX][12]; /* the strip's ST, RGBAQ, XYZF2 as kicked */
    float setup_vu[28];     /* an MSCAL's: vf01-vf04, vf05-vf07 after the run */
} Expect;

static struct {
    int on, limit;
    const uint8_t *vu_data;
    uint64_t runs;
    /* the run in flight */
    int pending;
    uint32_t n, ad_n;
    uint32_t tag[4];
    /* the runs gathered for the GPU */
    GsnNd700OracleDraw *draws;
    GsnNd700OracleBatch *batches;
    Expect *expect;
    GsnNd700OracleVertex *out;
    uint32_t count;
    /* the block VIF1 was last sent */
    int block_left;
    const char *block_why;
    /* the tallies */
    uint64_t done, mscals, swapped, no_strip, one_left, vertices, vertices_drawn, clipped_vertices, refused;
    uint64_t lane_differ[L_COUNT], lane_differ_clipped[L_COUNT];
    uint64_t kicks_differ, ad_differ, setup_compared, setup_differ[2];
    uint64_t blocks, block_batches, block_same, block_differ, runs_unparsed, past[4];
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

void nd700_oracle_begin(const uint8_t *vu_data, int limit) {
    o.on = 1;
    o.limit = limit;
    o.vu_data = vu_data;
    o.draws = calloc(RUNS_MAX, sizeof(*o.draws));
    o.batches = calloc(RUNS_MAX, sizeof(*o.batches));
    o.expect = calloc(RUNS_MAX, sizeof(*o.expect));
    o.out = calloc((size_t)RUNS_MAX * (N_MAX + 1), sizeof(*o.out));
    if (!o.draws || !o.batches || !o.expect || !o.out) {
        fprintf(stderr, "kf4-gsreplay: --nd-oracle: out of memory\n");
        exit(1);
    }
}

static void show(uint64_t run, uint32_t k, uint32_t n, int clipped, int lane, uint32_t want, uint32_t got) {
    if (o.shown >= o.limit) {
        return;
    }
    o.shown++;
    printf("nd-oracle 0x700: run %llu vertex %u/%u%s: lane %s: VU1 %08X, native %08X", (unsigned long long)run, k, n,
           clipped ? " (not drawn)" : "", lane_names[lane], want, got);
    if (lane == L_S || lane == L_T || lane == L_Q || lane == L_PS || lane == L_PT || lane == L_PQ) {
        printf(" (%.9g, %.9g)", (double)bits_float(want), (double)bits_float(got));
    }
    printf("\n");
}

static void lane(uint64_t run, uint32_t k, uint32_t n, int clipped, int l, uint32_t want, uint32_t got) {
    if (want == got) {
        return;
    }
    if (clipped) {
        o.lane_differ_clipped[l]++;
        if (o.shown_undrawn < 12) {
            int shown = o.shown;

            o.shown_undrawn++;
            o.shown = 0;
            show(run, k, n, 1, l, want, got);
            o.shown = shown;
        }
    } else {
        o.lane_differ[l]++;
        show(run, k, n, 0, l, want, got);
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
    const GsnNd700OracleBatch *b = &o.batches[i];
    const float *vu = o.expect[i].setup_vu;
    static const char *names[2] = {"vertex matrix", "light directions"};
    const float *mine[2] = {b->setup_vertex, b->setup_light_dir};
    static const int at[2] = {0, 16}, count[2] = {16, 12};
    int part, j;

    o.setup_compared++;
    for (part = 0; part < 2; part++) {
        if (memcmp(mine[part], vu + at[part], (size_t)count[part] * 4) == 0) {
            continue;
        }
        o.setup_differ[part]++;
        if (o.shown < o.limit) {
            o.shown++;
            printf("nd-oracle 0x700: set-up of run %llu: %s differ:", (unsigned long long)o.expect[i].run, names[part]);
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

    if (!o.count) {
        return;
    }
    if (!gsn_nd700_oracle_run(o.draws, o.count, o.batches, o.out)) {
        fprintf(stderr, "kf4-gsreplay: --nd-oracle: the native GS cannot run the 0x700 oracle\n");
        exit(1);
    }
    for (i = 0; i < o.count; i++) {
        const GsnNd700OracleDraw *d = &o.draws[i];
        const GsnNd700OracleBatch *b = &o.batches[i];
        const Expect *e = &o.expect[i];
        uint32_t want_packets, prim;

        if (!b->ok) {
            o.refused++;
            if (o.shown < o.limit) {
                o.shown++;
                printf("nd-oracle 0x700: run %llu: the parser refuses its batch: %s\n", (unsigned long long)e->run,
                       b->why ? b->why : "?");
            }
            continue;
        }
        o.done++;
        o.vertices += b->n;
        o.mscals += d->mscal != 0;
        o.swapped += b->swapped != 0;
        o.no_strip += !b->kick;
        o.one_left += b->one_left != 0;
        if (b->one_left && o.shown < o.limit) {
            /* (vertex n decides the kick: the game's blocks never hold it, gsn_nd_700.inc takes it clipped) */
            o.shown++;
            printf("nd-oracle 0x700: run %llu: one vertex of %u left unclipped; vertex n clipped %d; VU1 kicked the "
                   "strip %d\n",
                   (unsigned long long)e->run, b->n, b->clip_past, e->strip);
        }
        if (d->mscal) {
            compare_setup(i);
        }
        /* the kicks: FOGCOL (an MSCAL's), the A+D packet, then the strip */
        want_packets = (d->mscal ? 1u : 0u) + 1 + (b->kick ? 1 : 0);
        if (e->packets != want_packets || e->strip != b->kick) {
            o.kicks_differ++;
            if (o.shown < o.limit) {
                o.shown++;
                printf("nd-oracle 0x700: run %llu: VU1 kicked %u packets (strip %d), the plan %u (kick %d, one vertex "
                       "left %d, vertex n clipped %d)\n",
                       (unsigned long long)e->run, e->packets, e->strip, want_packets, b->kick, b->one_left,
                       b->clip_past);
            }
        }
        /* the A+D packet's writes (value, register a qword) against the plan's */
        {
            int same = e->ad_qwords == 1 + b->ad_count / 2;
            uint32_t j;

            for (j = 0; same && j < b->ad_count / 2; j++) {
                uint64_t value = (uint64_t)e->ad[1 + j][0] | (uint64_t)e->ad[1 + j][1] << 32;

                same = value == b->ad[2 * j + 1] && (e->ad[1 + j][2] & 0xFF) == b->ad[2 * j];
            }
            if (!same) {
                o.ad_differ++;
                if (o.shown < o.limit) {
                    o.shown++;
                    printf("nd-oracle 0x700: run %llu: the A+D packet differs (TEX0 replaced by the plan: %d)\n",
                           (unsigned long long)e->run, b->swapped);
                }
            }
        }
        if (!e->strip) {
            continue;
        }
        prim = (d->batch[0][1] >> 15) & 7;
        for (k = 0; k < b->n; k++) {
            const GsnNd700OracleVertex *v = &o.out[b->out + k];
            const uint32_t *st = e->hle[k], *rgbaq = st + 4, *xyzf = st + 8;
            int clipped = !drawn((const uint32_t(*)[12])e->hle, b->n, prim, k);
            uint64_t run = e->run;
            uint32_t s, t, q;
            int data = (d->batch[2 + 4 * k][3] & 0x8000) != 0; /* (the normal's w: the data's ADC) */

            if (clipped) {
                o.clipped_vertices++;
            } else {
                o.vertices_drawn++;
            }
            lane(run, k, b->n, clipped, L_X, xyzf[0] & 0xFFFF, v->gpu[0] & 0xFFFF);
            lane(run, k, b->n, clipped, L_Y, xyzf[1] & 0xFFFF, v->gpu[1] & 0xFFFF);
            lane(run, k, b->n, clipped, L_Z, (xyzf[2] >> 4) & 0xFFFFFF, v->gpu[2]);
            lane(run, k, b->n, clipped, L_F, (xyzf[3] >> 4) & 0xFF, (v->gpu[3] >> 4) & 0xFF);
            lane(run, k, b->n, clipped, L_W, xyzf[3] & 0x700F, v->gpu[3] & 0x700F);
            lane(run, k, b->n, clipped, L_R, rgbaq[0] & 0xFF, v->gpu[4] & 0xFF);
            lane(run, k, b->n, clipped, L_G, rgbaq[1] & 0xFF, (v->gpu[4] >> 8) & 0xFF);
            lane(run, k, b->n, clipped, L_B, rgbaq[2] & 0xFF, (v->gpu[4] >> 16) & 0xFF);
            lane(run, k, b->n, clipped, L_A, rgbaq[3] & 0xFF, v->gpu[4] >> 24);
            lane(run, k, b->n, clipped, L_S, st[0], v->gpu[5]);
            lane(run, k, b->n, clipped, L_T, st[1], v->gpu[6]);
            lane(run, k, b->n, clipped, L_Q, st[2], v->gpu[7]);
            lane(run, k, b->n, clipped, L_ADC, (xyzf[3] >> 15) & 1, (uint32_t)(v->plan_adc || data));
            lane(run, k, b->n, clipped, L_PX, xyzf[0] & 0xFFFF, (uint32_t)v->plan_xyz[0] & 0xFFFF);
            lane(run, k, b->n, clipped, L_PY, xyzf[1] & 0xFFFF, (uint32_t)v->plan_xyz[1] & 0xFFFF);
            lane(run, k, b->n, clipped, L_PZ, (xyzf[2] >> 4) & 0xFFFFFF, ((uint32_t)v->plan_xyz[2] >> 4) & 0xFFFFFF);
            memcpy(&s, &v->plan_stq[0], 4);
            memcpy(&t, &v->plan_stq[1], 4);
            memcpy(&q, &v->plan_stq[2], 4);
            lane(run, k, b->n, clipped, L_PS, st[0], s);
            lane(run, k, b->n, clipped, L_PT, st[1], t);
            lane(run, k, b->n, clipped, L_PQ, st[2], q);
            lane(run, k, b->n, clipped, L_PA, rgbaq[3] & 0xFF, v->plan_alpha);
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

void nd700_oracle_vifdata(const uint32_t *words, size_t count, uint32_t program) {
    int batches;

    if (!o.on || count % 4) {
        return;
    }
    batches = gsn_nd700_oracle_block(words, (uint32_t)(count / 4), program, &o.block_why);
    o.block_left = batches;
    if (batches > 0) {
        o.blocks++;
        o.block_batches += (uint64_t)batches;
    }
}

void nd700_oracle_vu1_run(uint32_t program, int mscal, uint32_t top) {
    GsnNd700OracleDraw *d;
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
    if (program != 0x700) {
        return;
    }
    top &= 0x3FF;
    n = qw(top)[0] & 0x7FFF;
    if (n < 3 || n > N_MAX || top < 0x80 || top + 0xC9 + 3 * (n + 1) > 0x400) {
        return; /* (the interpreter's, not the HLE's: none in the game) */
    }
    d = &o.draws[o.count];
    memset(d, 0, sizeof(*d));
    d->mscal = (uint32_t)mscal;
    d->top = top;
    memcpy(d->mem, o.vu_data, sizeof(d->mem));
    port_vu1_debug_regs(&regs, 0);
    memcpy(d->vf, regs.vf, sizeof(d->vf));
    for (i = 0; i < GSN_ND700_ORACLE_BATCH_QWORDS; i++) {
        memcpy(d->batch[i], qw(top + i), 16);
    }
    memset(&o.expect[o.count], 0, sizeof(o.expect[o.count]));
    o.expect[o.count].run = o.runs;
    memcpy(o.tag, d->batch[0], 16);
    o.n = n;
    o.ad_n = d->batch[0xA0][0] & 0x7FFF;
    o.pending = 1;
    if (from_block) {
        const char *what;
        int past;
        int same = gsn_nd700_oracle_block_check(d, &past, &what);

        if (same == 1) {
            o.block_same++;
        } else if (same == 0) {
            o.block_differ++;
            if (o.shown < o.limit) {
                o.shown++;
                printf("nd-oracle 0x700: run %llu: the block's batch differs from the run's: %s\n",
                       (unsigned long long)o.runs, what);
            }
        }
        if (same >= 0) {
            o.past[past & 3]++;
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

void nd700_oracle_gif(int path, const void *data, size_t bytes) {
    Expect *e;
    const uint32_t *w = data;

    if (!o.pending || path != 1) {
        return;
    }
    e = &o.expect[o.count];
    if (e->packets < PACKETS_MAX) {
        e->sizes[e->packets] = (uint32_t)bytes;
    }
    e->packets++;
    if (o.draws[o.count].mscal) {
        /* VU1's set-up (done before any packet but FOGCOL's: what the last packet saw is kept) */
        Kf4GsVu1Regs regs;
        int j;

        port_vu1_debug_regs(&regs, 0);
        for (j = 0; j < 4; j++) {
            memcpy(e->setup_vu + 4 * j, regs.vf[1 + j], 16);
        }
        for (j = 0; j < 3; j++) {
            memcpy(e->setup_vu + 16 + 4 * j, regs.vf[5 + j], 16);
        }
    }
    if (bytes == 16 + 48 * (size_t)o.n && memcmp(w, o.tag, 16) == 0 && !e->strip) {
        e->strip = 1;
        memcpy(e->hle, w + 4, 48 * (size_t)o.n);
        return;
    }
    if (bytes == 16 * (1 + (size_t)o.ad_n) && o.ad_n <= 16 && !e->strip) {
        e->ad_qwords = 1 + o.ad_n;
        memcpy(e->ad, w, bytes);
    }
}

void nd700_oracle_end(void) {
    uint64_t total = 0, total_clipped = 0;
    int l;
    uint32_t i;

    if (!o.on) {
        return;
    }
    finish();
    flush();
    printf("nd-oracle 0x700: %llu runs (%llu MSCALs), %llu vertices; %llu with TEX0 replaced, %llu without a strip, "
           "%llu with one vertex left unclipped; %llu runs' batches refused by the parser\n",
           (unsigned long long)o.done, (unsigned long long)o.mscals, (unsigned long long)o.vertices,
           (unsigned long long)o.swapped, (unsigned long long)o.no_strip, (unsigned long long)o.one_left,
           (unsigned long long)o.refused);
    printf("nd-oracle 0x700: %llu vertices drawn, %llu not; lanes differing (drawn/not drawn):",
           (unsigned long long)o.vertices_drawn, (unsigned long long)o.clipped_vertices);
    for (l = 0; l < L_COUNT; l++) {
        printf(" %s %llu/%llu", lane_names[l], (unsigned long long)o.lane_differ[l],
               (unsigned long long)o.lane_differ_clipped[l]);
        total += o.lane_differ[l];
        total_clipped += o.lane_differ_clipped[l];
    }
    printf("\n");
    printf("nd-oracle 0x700: kicks differing in %llu runs, A+D packets in %llu; set-up: %llu MSCALs compared; "
           "differing: vertex matrix %llu, light directions %llu\n",
           (unsigned long long)o.kicks_differ, (unsigned long long)o.ad_differ, (unsigned long long)o.setup_compared,
           (unsigned long long)o.setup_differ[0], (unsigned long long)o.setup_differ[1]);
    printf("nd-oracle 0x700: parser on the game's blocks: %llu blocks parsed (%llu batches); runs: %llu the same as "
           "their block's batch, %llu differ, %llu not from a parsed block",
           (unsigned long long)o.blocks, (unsigned long long)o.block_batches, (unsigned long long)o.block_same,
           (unsigned long long)o.block_differ, (unsigned long long)o.runs_unparsed);
    for (i = 0; i < 16 && o.unparsed_why[i].why; i++) {
        printf("%s %llu: \"%s\"", i ? "," : " (the last block's", (unsigned long long)o.unparsed_why[i].count,
               o.unparsed_why[i].why);
    }
    printf("%s\n", o.unparsed_why[0].why ? ")" : "");
    printf("nd-oracle 0x700: vertex n in the block's batches: %llu known and VU1's, %llu not known, %llu known but "
           "not VU1's\n",
           (unsigned long long)o.past[1], (unsigned long long)o.past[2], (unsigned long long)o.past[3]);
    printf("nd-oracle 0x700: %llu lane differences (%llu more on vertices not drawn)\n", (unsigned long long)total,
           (unsigned long long)total_clipped);
}
