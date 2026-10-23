/* The vertex oracle of native draw for VU1's program 0x280 (docs/port/NATIVE_DRAW_TASKS.md, P2-2): the
   runs of 0x280 a capture replays (kf4-gsreplay --nd-oracle: tools/gsreplay/nd_oracle.c) also through
   native draw: the parser's batch made from VU1 memory as the run found it (render.cpp, take_batch_280),
   the CPU's set-up and plan (gsn_nd_280.inc: nd280_const, nd280_plan) and shaders/nd_280.vert on the GPU
   (its ND_ORACLE build), both passes; the caller compares them with what the run kicked. And the blocks
   VIF1 is sent, parsed as the game's would be (RD_VF_FLOAT_SKIN), against the runs' own batches. A
   debugging tool: the game never calls it. */
#ifndef GSN_ND280_ORACLE_H
#define GSN_ND280_ORACLE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GSN_ND280_ORACLE_BATCH_QWORDS 0xC0 /* TOP .. TOP+0xBF: the tags, vertices and both A+D packets */

/* A VU1 run of program 0x280 as it started */
typedef struct {
    uint32_t mscal;     /* an MSCAL 0x280: the set-up runs first (from the fixed block); else an MSCNT */
    uint32_t top;
    uint32_t pad[2];
    float mem[0x74][4]; /* VU1 data memory 0x00-0x73 as the run found it */
    float vf[12][4];    /* vf00-vf11 as the run found them (an MSCNT's: the light colours of the set-up before) */
    uint32_t batch[GSN_ND280_ORACLE_BATCH_QWORDS][4]; /* data memory from TOP on */
} GsnNd280OracleDraw;

/* What native draw made of a run */
typedef struct {
    int32_t ok;          /* its batch was made (else why: the parser's reason) */
    const char *why;
    uint32_t n, out;     /* its vertices; its first in out (a vertex's two passes each) */
    int32_t sphere;      /* it has a second pass */
    int32_t all_clipped; /* the plan: the run ends without a strip (and without the second pass) */
    int32_t adc_tag_odd; /* the tag's w is odd (with vertex 0 clipped, every vertex has ADC) */
    /* an MSCAL's set-up as the CPU made it, to compare with VU1 memory after the run: 0x14-0x17 and
       0x1C-0x1F, 0x18-0x1A and 0x20-0x22, 0x4A-0x4D and 0x53-0x56, 0x2C-0x2E and 0x30-0x32, 0x72-0x73 */
    float setup_vertex[2][16];
    float setup_light_dir[2][12];
    float setup_point_pos[2][16];
    float setup_sphere[2][12];
    float setup_colour[2][4];
} GsnNd280OracleBatch;

/* A vertex of a pass: what nd_280.vert gave the GS, and the CPU's plan of it */
typedef struct {
    uint32_t gpu[8];     /* X and Y (FTOI4), Z (24 bits), XYZF2's w; RGBA (a byte each), S, T, Q (bits) */
    int32_t plan_xyz[3]; /* the plan's FTOI4 of the place */
    float plan_stq[3];
    uint32_t plan_alpha; /* the corner's alpha (what flush_batch decides by) */
    int32_t plan_clip;   /* the plan's clip judgement */
    int32_t plan_adc;    /* the plan's ADC (clipping's, and the tag's; not the data's) */
    int32_t pad;
} GsnNd280OracleVertex;

/* Runs draw_count runs into batches[i] and out (2 * n a run: a vertex's first pass, then its second;
   a run of no second pass leaves the second's gpu at 0). draw_count at most 1 << 15. 1 when done. */
int gsn_nd280_oracle_run(const GsnNd280OracleDraw *draws, uint32_t draw_count, GsnNd280OracleBatch *batches,
                         GsnNd280OracleVertex *out);
/* A block VIF1 is sent (one DMA transfer), parsed as rd_mesh_from_block parses RD_VF_FLOAT_SKIN; program:
   the VU1 program last started (an MSCNT goes on with it). Its batches are kept for the runs it starts:
   returns how many (0: refused, why says why, or not 0x280's) */
int gsn_nd280_oracle_block(const void *words, uint32_t qwords, uint32_t program, const char **why);
/* The next of those batches against the run's own: 1 the same, 0 not (what differs), -1 none left */
int gsn_nd280_oracle_block_check(const GsnNd280OracleDraw *run, const char **what);

#ifdef __cplusplus
}
#endif
#endif /* GSN_ND280_ORACLE_H */
