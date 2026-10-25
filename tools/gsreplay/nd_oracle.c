/* kf4-gsreplay --nd-oracle: native draw's vertex oracle (docs/port/NATIVE_DRAW_TASKS.md, P0-2).

   With --mode vif, every run of VU1's program 0x000 (an MSCAL 0x000, or an MSCNT that goes on with it)
   is taken as it starts: the fixed block and the batch's vertices in VU1 memory, and the registers. The
   run's last XGKICK (the strip at TOP+0xC8: a GIF tag, then ST, RGBAQ and XYZF2 a vertex) is what VU1
   made (--vu1 hle: port/platform/vu1_hle.c, the specification; interp: the interpreter). The same
   vertices go through native draw (port/gs/native/gsn_nd_oracle.h): nd_000.vert on the GPU (built with
   ND_ORACLE, writing out what each corner gives the GS), and the CPU's plan of their places
   (nd_plan_vertex) and set-up (nd_const). Each lane is compared, bit for bit:
     X, Y   XYZF2's X and Y as the GS takes them (the low 16 bits of FTOI4)
     Z      XYZF2's Z (bits 4-27)
     F      the fog (bits 4-11 of XYZF2's w)
     w      the rest of XYZF2's w but ADC (the normal's w: bits 0-3 and 12-14)
     R G B A  RGBAQ's bytes (after the point lights)
     S T Q  ST's floats' bits
     ADC    the plan's (the vertex's clip judgement or either of the two before it, or the data's) against
            the run's (whose first two vertices also count the "vertex -1" the pipeline tests:
            vu1_hle.c; no triangle is kicked at either, so it does not change what is drawn)
     plan X Y Z S T Q   the CPU plan's place (what flush_batch decides by), as X Y Z S T Q
     set-up (MSCALs): the vertex matrix (VU1 0x14-0x17), the light directions (vf05-07) and the point
            lights (0x4A-0x4D) as the CPU's set-up made them, against VU1's after the run
   A vertex that VU1's strip does not draw (every triangle it is in has ADC: clipped, or the data's
   restarts) cannot change the picture: its differences are counted apart ("not drawn").

   Program 0x5A0 (the shadows' silhouettes, port/platform/vu1_hle_5a0.c) is drawn by the same shader from
   the parser's batch (render.cpp, take_batch's silhouette) and a record without lights or fog
   (gsn_nd_5a0.inc): its runs go through the same comparison, the vertices as the parser makes them of
   VU1 memory, the set-up's vertex matrix against VU1's vf01-vf04 after the run (0x5A0 does not store it),
   and are tallied apart too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/debug.h"
#include "port/gs_capture_format.h"
#include "gsn_nd_oracle.h"
#include "nd_oracle.h"

#define BATCH_VERTICES (1u << 16) /* vertices a GPU run */
#define N_MAX 39

enum {
    L_X, L_Y, L_Z, L_F, L_W, L_R, L_G, L_B, L_A, L_S, L_T, L_Q, L_ADC,
    L_PX, L_PY, L_PZ, L_PS, L_PT, L_PQ,
    L_COUNT
};
static const char *lane_names[L_COUNT] = {"X", "Y", "Z", "F", "w", "R", "G", "B", "A", "S", "T", "Q", "ADC",
                                          "plan X", "plan Y", "plan Z", "plan S", "plan T", "plan Q"};

typedef struct {
    uint32_t hle[12];  /* ST, RGBAQ, XYZF2 as kicked */
    uint32_t draw;     /* in the GPU batch */
    uint32_t k, n;     /* the vertex in its strip, the strip's vertices */
    uint64_t run;      /* the VU1 run (from the capture's start) */
    uint32_t program;  /* 0x000 or 0x5A0 */
    int clipped;       /* VU1's strip does not draw it (every triangle it is in has ADC) */
} Expect;

static struct {
    int on, limit;
    const uint8_t *vu_data;
    /* the run in flight */
    int pending;
    uint32_t top, n;
    uint32_t tag[4];
    GsnNdOracleDraw draw;
    int32_t verts[N_MAX][16];
    uint64_t runs;
    /* the GPU batch being gathered */
    GsnNdOracleDraw *draws;
    GsnNdOracleSetup *setups;
    float (*setup_vu)[44]; /* VU1's: 0x14-0x17, vf05-07, 0x4A-0x4D */
    uint32_t draw_count;
    int32_t (*vertices)[16];
    uint32_t *vertex_draw;
    Expect *expect;
    GsnNdOracleVertex *out;
    uint32_t vertex_count;
    /* the tallies */
    uint64_t vertices_done, batches, mscals, lit_batches, lit_vertices, fogged_vertices, clipped_vertices;
    uint64_t lane_differ[L_COUNT], lane_differ_clipped[L_COUNT];
    uint64_t adc_vertex_minus_1; /* ADC differs at vertex 0 or 1 only by the pipeline's vertex -1 */
    uint64_t setup_compared, setup_differ[3];
    uint64_t not_matched; /* runs whose strip was not met */
    uint64_t undrawn_differ, undrawn_differ_w0; /* vertices not drawn that differ; of them, VU1 overflowed */
    int shown, shown_undrawn;
    /* program 0x5A0's own tallies (its lanes are in the ones above too) */
    uint64_t batches_5a0, mscals_5a0, vertices_5a0, clipped_5a0, differ_5a0, differ_5a0_undrawn, refused_5a0;
    uint64_t setup_differ_5a0;
    const char *why_5a0;
} o;

void nd_oracle_begin(const uint8_t *vu_data, int limit) {
    o.on = 1;
    o.limit = limit;
    o.vu_data = vu_data;
    o.draws = calloc(BATCH_VERTICES / 3 + 1, sizeof(*o.draws));
    o.setups = calloc(BATCH_VERTICES / 3 + 1, sizeof(*o.setups));
    o.setup_vu = calloc(BATCH_VERTICES / 3 + 1, sizeof(*o.setup_vu));
    o.vertices = calloc(BATCH_VERTICES, sizeof(*o.vertices));
    o.vertex_draw = calloc(BATCH_VERTICES, sizeof(*o.vertex_draw));
    o.expect = calloc(BATCH_VERTICES, sizeof(*o.expect));
    o.out = calloc(BATCH_VERTICES, sizeof(*o.out));
    nd280_oracle_begin(vu_data, limit); /* (program 0x280's: nd_oracle_280.c) */
    nd700_oracle_begin(vu_data, limit); /* (program 0x700's: nd_oracle_700.c) */
    if (!o.draws || !o.setups || !o.setup_vu || !o.vertices || !o.vertex_draw || !o.expect || !o.out) {
        fprintf(stderr, "kf4-gsreplay: --nd-oracle: out of memory\n");
        exit(1);
    }
}

int nd_oracle_on(void) {
    return o.on;
}

static const uint32_t *qw(uint32_t address) {
    return (const uint32_t *)(o.vu_data + (size_t)(address & 0x3FF) * 16);
}

static float bits_float(uint32_t u) {
    float f;

    memcpy(&f, &u, 4);
    return f;
}

static void show(const char *what, const Expect *e, const GsnNdOracleVertex *v, int lane, uint32_t want, uint32_t got) {
    if (o.shown >= o.limit) {
        return;
    }
    o.shown++;
    printf("nd-oracle: run %llu vertex %u/%u%s: %s %s: VU1 %08X, native %08X", (unsigned long long)e->run, e->k, e->n,
           e->clipped ? " (not drawn)" : "", what, lane_names[lane], want, got);
    if (lane == L_S || lane == L_T || lane == L_Q || lane == L_PS || lane == L_PT || lane == L_PQ) {
        printf(" (%.9g, %.9g)", (double)bits_float(want), (double)bits_float(got));
    }
    (void)v;
    printf("\n");
}

static void lane(const Expect *e, const GsnNdOracleVertex *v, int l, uint32_t want, uint32_t got) {
    if (want == got) {
        return;
    }
    if (e->program == 0x5A0) {
        if (e->clipped) {
            o.differ_5a0_undrawn++;
        } else {
            o.differ_5a0++;
        }
    }
    if (e->clipped) {
        o.lane_differ_clipped[l]++;
        if (o.shown_undrawn < 12) {
            /* (a few of these too, to say why) */
            int shown = o.shown;

            o.shown_undrawn++;
            o.shown = 0;
            show("lane", e, v, l, want, got);
            o.shown = shown;
        }
    } else {
        o.lane_differ[l]++;
        show("lane", e, v, l, want, got);
    }
}

/* VU1's arithmetic went past the floats for the vertex: it has no infinity, a result too large is the
   largest float (and exponent 255 an ordinary number), FTOI the largest integer, where the GPU's IEEE
   floats go to infinity and NaN. Seen in what VU1 kicked: 1/w (Q) the largest float (w 0), an integer of
   XYZF2 saturated, or an ST float with exponent 255. Such a vertex is far past the guard band, clipped,
   and never drawn. */
static int vu_overflowed(const uint32_t *hle) {
    int i;

    for (i = 0; i < 3; i++) {
        if ((hle[i] & 0x7F800000u) == 0x7F800000u || hle[8 + i] == 0x7FFFFFFFu || hle[8 + i] == 0x80000000u) {
            return 1;
        }
    }
    return 0;
}

/* the GPU batch gathered so far: run and compared */
static void flush(void) {
    uint32_t i;

    if (!o.vertex_count) {
        return;
    }
    if (!gsn_nd_oracle_run(o.draws, o.draw_count, (const int32_t(*)[16])o.vertices, o.vertex_draw, o.vertex_count,
                           o.setups, o.out)) {
        fprintf(stderr, "kf4-gsreplay: --nd-oracle: the native GS cannot run the oracle\n");
        exit(1);
    }
    for (i = 0; i < o.draw_count; i++) {
        const float *mine = o.setups[i].vertex;
        int part;

        if (!o.draws[i].mscal) {
            continue;
        }
        o.setup_compared++;
        /* the vertex matrix, the light directions, the point lights (0x5A0: the vertex matrix only) */
        for (part = 0; part < (o.draws[i].program == 0x5A0 ? 1 : 3); part++) {
            static const int from[3] = {0, 16, 28}, count[3] = {16, 12, 16};

            if (memcmp(mine + from[part], o.setup_vu[i] + from[part], (size_t)count[part] * 4) != 0) {
                o.setup_differ[part]++;
                o.setup_differ_5a0 += o.draws[i].program == 0x5A0;
                if (o.shown < o.limit) {
                    static const char *names[3] = {"vertex matrix", "light directions", "point lights"};
                    int j;

                    o.shown++;
                    printf("nd-oracle: set-up of a batch: %s differs:", names[part]);
                    for (j = 0; j < count[part]; j++) {
                        uint32_t a, b;

                        memcpy(&a, o.setup_vu[i] + from[part] + j, 4);
                        memcpy(&b, mine + from[part] + j, 4);
                        if (a != b) {
                            printf(" [%d] %08X/%08X", j, a, b);
                        }
                    }
                    printf("\n");
                }
            }
        }
    }
    for (i = 0; i < o.vertex_count; i++) {
        const Expect *e = &o.expect[i];
        const GsnNdOracleVertex *v = &o.out[i];
        uint64_t before = 0;
        int l;

        for (l = 0; l < L_COUNT; l++) {
            before += o.lane_differ_clipped[l];
        }
        const uint32_t *st = e->hle, *rgbaq = e->hle + 4, *xyzf = e->hle + 8;
        uint32_t nw = (uint32_t)o.vertices[i][7] & 0xFFFF;
        int plan_adc, hle_adc;

        lane(e, v, L_X, xyzf[0] & 0xFFFF, v->gpu[0] & 0xFFFF);
        lane(e, v, L_Y, xyzf[1] & 0xFFFF, v->gpu[1] & 0xFFFF);
        lane(e, v, L_Z, (xyzf[2] >> 4) & 0xFFFFFF, v->gpu[2]);
        lane(e, v, L_F, (xyzf[3] >> 4) & 0xFF, (v->gpu[3] >> 4) & 0xFF);
        lane(e, v, L_W, xyzf[3] & 0x700F, v->gpu[3] & 0x700F);
        lane(e, v, L_R, rgbaq[0] & 0xFF, v->gpu[4] & 0xFF);
        lane(e, v, L_G, rgbaq[1] & 0xFF, (v->gpu[4] >> 8) & 0xFF);
        lane(e, v, L_B, rgbaq[2] & 0xFF, (v->gpu[4] >> 16) & 0xFF);
        lane(e, v, L_A, rgbaq[3] & 0xFF, v->gpu[4] >> 24);
        lane(e, v, L_S, st[0], v->gpu[5]);
        lane(e, v, L_T, st[1], v->gpu[6]);
        lane(e, v, L_Q, st[2], v->gpu[7]);
        /* the plan's ADC: this vertex's clip or either of the two before it in the strip, or the data's */
        plan_adc = v->plan_clip || (e->k >= 1 && o.out[i - 1].plan_clip) || (e->k >= 2 && o.out[i - 2].plan_clip) ||
                   (nw & 0x8000);
        hle_adc = (xyzf[3] & 0x8000) != 0;
        if (plan_adc != hle_adc && e->k < 2 && hle_adc && !plan_adc) {
            o.adc_vertex_minus_1++; /* (vertex -1's: no triangle is kicked at vertex 0 or 1) */
        } else {
            lane(e, v, L_ADC, (uint32_t)hle_adc, (uint32_t)plan_adc);
        }
        lane(e, v, L_PX, xyzf[0] & 0xFFFF, (uint32_t)v->plan_xyz[0] & 0xFFFF);
        lane(e, v, L_PY, xyzf[1] & 0xFFFF, (uint32_t)v->plan_xyz[1] & 0xFFFF);
        lane(e, v, L_PZ, (xyzf[2] >> 4) & 0xFFFFFF, ((uint32_t)v->plan_xyz[2] >> 4) & 0xFFFFFF);
        {
            uint32_t s, t, q;

            memcpy(&s, &v->plan_stq[0], 4);
            memcpy(&t, &v->plan_stq[1], 4);
            memcpy(&q, &v->plan_stq[2], 4);
            lane(e, v, L_PS, st[0], s);
            lane(e, v, L_PT, st[1], t);
            lane(e, v, L_PQ, st[2], q);
        }
        if (e->clipped) {
            uint64_t after = 0;

            o.clipped_vertices++;
            o.clipped_5a0 += e->program == 0x5A0;
            for (l = 0; l < L_COUNT; l++) {
                after += o.lane_differ_clipped[l];
            }
            if (after != before) {
                o.undrawn_differ++;
                o.undrawn_differ_w0 += vu_overflowed(e->hle);
            }
        }
        if (((xyzf[3] >> 4) & 0xFF) != 0xFF) {
            o.fogged_vertices++;
        }
    }
    o.vertices_done += o.vertex_count;
    o.vertex_count = 0;
    o.draw_count = 0;
}

/* A VU1 run starts (vif1.c, before it): program is the program it runs (an MSCNT's: the one before) */
void nd_oracle_vu1_run(uint32_t program, int mscal, uint32_t top) {
    Kf4GsVu1Regs regs;
    uint32_t k, n;

    if (o.pending) {
        o.not_matched++;
        o.pending = 0;
    }
    o.runs++;
    nd280_oracle_vu1_run(program, mscal, top);
    nd700_oracle_vu1_run(program, mscal, top);
    if (program != 0 && program != 0x5A0) {
        return;
    }
    top &= 0x3FF;
    n = qw(top)[0] & 0x7FFF;
    if (n < 3 || n > N_MAX || top < 0x80 || top + (program == 0x5A0 ? 0xC9 : 0xD1) + 3 * n > 0x400) {
        return; /* (the interpreter's, not the HLE's: none in the game) */
    }
    if (program == 0x5A0) {
        /* (the vertices as the parser stores them: the colour 0, ST's z 1 but the last two's) */
        const char *why;

        if (gsn_nd_oracle_5a0_vertices(o.vu_data, top, (uint32_t)mscal, o.verts, N_MAX, &why) != (int)n) {
            o.refused_5a0++;
            o.why_5a0 = why;
            return;
        }
    } else {
        for (k = 0; k < n; k++) {
            memcpy(o.verts[k], qw(top + 1 + 4 * k), 64);
        }
    }
    port_vu1_debug_regs(&regs, 0);
    memset(&o.draw, 0, sizeof(o.draw));
    o.draw.mscal = (uint32_t)mscal;
    o.draw.program = program;
    memcpy(o.draw.mem, o.vu_data, sizeof(o.draw.mem));
    memcpy(o.draw.vf, regs.vf, sizeof(o.draw.vf));
    memcpy(o.tag, qw(top), 16);
    o.top = top;
    o.n = n;
    o.pending = 1;
}

/* A packet VU1 kicked: the strip of the run in flight, if it is */
void nd_oracle_gif(int path, const void *data, size_t bytes) {
    const uint32_t *w = data;
    uint32_t k, d, first;

    nd280_oracle_gif(path, data, bytes);
    nd700_oracle_gif(path, data, bytes);
    if (!o.pending || path != 1 || bytes != 16 + 48 * (size_t)o.n || memcmp(w, o.tag, 16) != 0) {
        return;
    }
    o.pending = 0;
    if (o.vertex_count + o.n > BATCH_VERTICES) {
        flush();
    }
    d = o.draw_count++;
    o.draws[d] = o.draw;
    o.batches++;
    if (o.draw.program == 0x5A0) {
        o.batches_5a0++;
        o.mscals_5a0 += o.draw.mscal != 0;
        o.vertices_5a0 += o.n;
    }
    if (o.draw.mscal) {
        Kf4GsVu1Regs regs;

        /* VU1's set-up as it left it (0x5A0 keeps its vertex matrix in vf01-vf04 only) */
        o.mscals++;
        port_vu1_debug_regs(&regs, 0);
        if (o.draw.program == 0x5A0) {
            for (k = 0; k < 4; k++) {
                memcpy(o.setup_vu[d] + 4 * k, regs.vf[1 + k], 16);
            }
        } else {
            memcpy(o.setup_vu[d], qw(0x14), 64);
        }
        for (k = 0; k < 3; k++) {
            memcpy(o.setup_vu[d] + 16 + 4 * k, regs.vf[5 + k], 16);
        }
        memcpy(o.setup_vu[d] + 28, qw(0x4A), 64);
    }
    {
        int32_t lights;
        int32_t word;

        memcpy(&word, o.vu_data + 0x49 * 16 + 12, 4);
        lights = (int16_t)word;
        if (lights > 0) {
            o.lit_batches++;
            o.lit_vertices += o.n;
        }
    }
    first = o.vertex_count;
    for (k = 0; k < o.n; k++) {
        Expect *e = &o.expect[first + k];

        memcpy(o.vertices[first + k], o.verts[k], 64);
        o.vertex_draw[first + k] = d;
        memcpy(e->hle, w + 4 + 12 * k, 48);
        e->draw = d;
        e->k = k;
        e->n = o.n;
        e->run = o.runs;
        e->program = o.draw.program;
        e->clipped = 0;
    }
    /* whether VU1's strip draws the vertex: a triangle with it in is kicked without ADC (strips: the
       kicks at k, k+1 and k+2; lists: the kick of its three; fans and the rest: taken as drawn) */
    {
        uint32_t prim = (w[1] >> 15) & 7;

        for (k = 0; k < o.n; k++) {
            int drawn = prim != 3 && prim != 4;
            uint32_t j;

            for (j = k; j < k + 3 && j < o.n; j++) {
                if (j >= 2 && (prim != 3 || j % 3 == 2) && (prim != 3 || j - k < 3) && !(w[4 + 12 * j + 11] & 0x8000)) {
                    drawn = 1;
                }
            }
            if (prim == 3 && k % 3 != 2) {
                /* (a list's vertex is in one triangle only: the kick at the end of its three) */
                uint32_t kick = k - k % 3 + 2;

                drawn = kick < o.n && !(w[4 + 12 * kick + 11] & 0x8000);
            }
            o.expect[first + k].clipped = !drawn;
        }
    }
    o.vertex_count += o.n;
}

void nd_oracle_end(void) {
    int l;
    uint64_t total = 0, total_clipped = 0;

    if (!o.on) {
        return;
    }
    flush();
    printf("nd-oracle: %llu batches of program 0x000 (%llu MSCALs), %llu vertices; %llu batches (%llu vertices) "
           "with point lights, %llu vertices fogged (F < 255), %llu not drawn; %llu runs' strips not met\n",
           (unsigned long long)o.batches, (unsigned long long)o.mscals, (unsigned long long)o.vertices_done,
           (unsigned long long)o.lit_batches, (unsigned long long)o.lit_vertices, (unsigned long long)o.fogged_vertices,
           (unsigned long long)o.clipped_vertices, (unsigned long long)o.not_matched);
    printf("nd-oracle: lanes differing (drawn vertices/not drawn):");
    for (l = 0; l < L_COUNT; l++) {
        printf(" %s %llu/%llu", lane_names[l], (unsigned long long)o.lane_differ[l],
               (unsigned long long)o.lane_differ_clipped[l]);
        total += o.lane_differ[l];
        total_clipped += o.lane_differ_clipped[l];
    }
    printf("\nnd-oracle: ADC at vertex 0 or 1 by the pipeline's vertex -1 only: %llu\n",
           (unsigned long long)o.adc_vertex_minus_1);
    printf("nd-oracle: set-up: %llu MSCALs compared; differing: vertex matrix %llu, light directions %llu, point "
           "lights %llu\n",
           (unsigned long long)o.setup_compared, (unsigned long long)o.setup_differ[0],
           (unsigned long long)o.setup_differ[1], (unsigned long long)o.setup_differ[2]);
    printf("nd-oracle: %llu lane differences (%llu more on vertices not drawn: %llu vertices, %llu of them where VU1 "
           "overflowed)\n",
           (unsigned long long)total, (unsigned long long)total_clipped, (unsigned long long)o.undrawn_differ,
           (unsigned long long)o.undrawn_differ_w0);
    printf("nd-oracle: of them program 0x5A0 (the shadows): %llu batches (%llu MSCALs), %llu vertices (%llu not drawn); "
           "%llu lane differences on drawn vertices (%llu on vertices not drawn), %llu set-ups differing; %llu runs "
           "the parser refused%s%s\n",
           (unsigned long long)o.batches_5a0, (unsigned long long)o.mscals_5a0, (unsigned long long)o.vertices_5a0,
           (unsigned long long)o.clipped_5a0, (unsigned long long)o.differ_5a0, (unsigned long long)o.differ_5a0_undrawn,
           (unsigned long long)o.setup_differ_5a0, (unsigned long long)o.refused_5a0, o.why_5a0 ? ": " : "",
           o.why_5a0 ? o.why_5a0 : "");
    nd280_oracle_end();
    nd700_oracle_end();
}
