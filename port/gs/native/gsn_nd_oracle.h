/* The vertex oracle of native draw (docs/port/NATIVE_DRAW_TASKS.md, P0-2): VU1's program 0x000 run by
   shaders/nd_000.vert on the GPU (its ND_ORACLE build, which also writes out what each corner gives the
   GS), and by the CPU's plan (gsn_native_draw.inc, nd_plan_vertex) and set-up (nd_const), for the same
   VU memory as a VU1 run had; the caller compares them with what that run kicked (kf4-gsreplay
   --nd-oracle: tools/gsreplay/nd_oracle.c). A debugging tool: the game never calls it. */
#ifndef GSN_ND_ORACLE_H
#define GSN_ND_ORACLE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A VU1 run of program 0x000 as it started */
typedef struct {
    uint32_t mscal;     /* an MSCAL 0x000: the set-up runs first (from the fixed block); else an MSCNT */
    uint32_t pad[3];
    float mem[0x52][4]; /* VU1 data memory 0x00-0x51 as the run found it */
    float vf[12][4];    /* vf00-vf11 as the run found them (an MSCNT's: the set-up's before it) */
} GsnNdOracleDraw;

/* What the CPU's set-up made of an MSCAL's fixed block (nd_const), for comparing with VU1's */
typedef struct {
    float vertex[16];    /* VU1 0x14-0x17: the vertex matrix */
    float light_dir[12]; /* vf05-vf07: the light directions by the part's rotation */
    float point_pos[16]; /* VU1 0x4A-0x4D: the point lights in the part's space */
} GsnNdOracleSetup;

/* A vertex: what nd_000.vert gave the GS, and the CPU's plan of it */
typedef struct {
    uint32_t gpu[8];     /* X and Y (FTOI4, the whole integer), Z (24 bits), XYZF2's w (fog << 4 | the
                            normal's w); RGBA (a byte each), and S, T, Q (a float's bits each) */
    int32_t plan_xyz[3]; /* the plan's FTOI4 of the place */
    float plan_stq[3];
    int32_t plan_clip;   /* the plan's clip judgement */
    int32_t pad;
} GsnNdOracleVertex;

/* 1 if the oracle can run: a native GS whose GPU lets a vertex shader write a buffer */
int gsn_nd_oracle_ready(void);
/* Runs vertex_count vertices: vertices[i] (its four qwords as VU1 memory had them) of
   draws[vertex_draw[i]], into out[i]; setups[d] for each draw (an MSCAL's; zeros for an MSCNT's).
   draw_count at most 1 << 17. 1 when done. */
int gsn_nd_oracle_run(const GsnNdOracleDraw *draws, uint32_t draw_count, const int32_t (*vertices)[16],
                      const uint32_t *vertex_draw, uint32_t vertex_count, GsnNdOracleSetup *setups,
                      GsnNdOracleVertex *out);

#ifdef __cplusplus
}
#endif
#endif /* GSN_ND_ORACLE_H */
