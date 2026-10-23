/* The vertex oracle of native draw for VU1's program 0x700, the effects (docs/port/NATIVE_DRAW_TASKS.md,
   P4-1): the runs of 0x700 a capture replays (kf4-gsreplay --nd-oracle: tools/gsreplay/nd_oracle_700.c) also
   through native draw: the parser's batch made from VU1 memory as the run found it (render.cpp, take_batch),
   the CPU's set-up, plan and A+D writes (gsn_nd_700.inc: nd700_const, nd700_plan, nd700_ad) and
   shaders/nd_700.vert on the GPU (its ND_ORACLE build); the caller compares them with what the run kicked.
   And the blocks VIF1 is sent, parsed as the game's adapter would have them parsed (RD_VF_PACKED16), against
   the runs' own batches. A debugging tool: the game never calls it. */
#ifndef GSN_ND700_ORACLE_H
#define GSN_ND700_ORACLE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GSN_ND700_ORACLE_BATCH_QWORDS 0xC0 /* TOP .. TOP+0xBF: the tag, the vertices (and vertex n), the A+D packet */

/* A VU1 run of program 0x700 as it started */
typedef struct {
    uint32_t mscal;     /* an MSCAL 0x700: the set-up runs first (from the fixed block); else an MSCNT */
    uint32_t top;
    uint32_t pad[2];
    float mem[0x67][4]; /* VU1 data memory 0x00-0x66 as the run found it */
    float vf[32][4];    /* vf00-vf31 as the run found them (an MSCNT's: the set-up's before it, vf28 the colour) */
    uint32_t batch[GSN_ND700_ORACLE_BATCH_QWORDS][4]; /* data memory from TOP on */
} GsnNd700OracleDraw;

/* What native draw made of a run */
typedef struct {
    int32_t ok;          /* its batch was made (else why: the parser's reason) */
    const char *why;
    uint32_t n, out;     /* its vertices; its first in out */
    int32_t kick;        /* the plan: the strip is kicked (fewer than n of the n + 1 vertices clipped) */
    int32_t one_left;    /* exactly one vertex of the strip is unclipped (vertex n decides the kick) */
    int32_t clip_past;   /* the plan's judgement of vertex n */
    int32_t swapped;     /* TEX0 replaced by VU1 0x66 */
    uint32_t ad_count;   /* the A+D writes the plan makes (register, value ...: twice the packet's NLOOP) */
    uint32_t pad;
    uint64_t ad[32];
    /* an MSCAL's set-up as the CPU made it, to compare with VU1's registers after the run: vf01-vf04 (the
       vertex matrix), vf05-vf07 (the light directions) */
    float setup_vertex[16];
    float setup_light_dir[12];
} GsnNd700OracleBatch;

/* A vertex: what nd_700.vert gave the GS, and the CPU's plan of it */
typedef struct {
    uint32_t gpu[8];     /* X and Y (FTOI4), Z (24 bits), XYZF2's w; RGBA (a byte each), S, T, Q (bits) */
    int32_t plan_xyz[3]; /* the plan's FTOI4 of the place */
    float plan_stq[3];
    uint32_t plan_alpha; /* the corner's alpha (what flush_batch decides by) */
    int32_t plan_clip;   /* the plan's clip judgement */
    int32_t plan_adc;    /* the plan's ADC (clipping's; not the data's) */
    int32_t pad;
} GsnNd700OracleVertex;

/* Runs draw_count runs into batches[i] and out (n a run). draw_count at most 1 << 15. 1 when done. */
int gsn_nd700_oracle_run(const GsnNd700OracleDraw *draws, uint32_t draw_count, GsnNd700OracleBatch *batches,
                         GsnNd700OracleVertex *out);
/* A block VIF1 is sent (one DMA transfer), parsed as rd_mesh_from_block parses RD_VF_PACKED16; program: the
   VU1 program last started (an MSCNT goes on with it). Its batches are kept for the runs it starts: returns
   how many (0: refused, why says why, or not 0x700's) */
int gsn_nd700_oracle_block(const void *words, uint32_t qwords, uint32_t program, const char **why);
/* The next of those batches against the run's own: 1 the same, 0 not (what differs), -1 none left. past:
   vertex n as the block's batch has it: 1 known and VU1's, 2 not known (the block did not write it), 3 known
   but not what VU1 memory holds */
int gsn_nd700_oracle_block_check(const GsnNd700OracleDraw *run, int *past, const char **what);

#ifdef __cplusplus
}
#endif
#endif /* GSN_ND700_ORACLE_H */
