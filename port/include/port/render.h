/* The renderer core of native draw (docs/port/NATIVE_DRAW.md, "Architecture"): what the game's patched
   draw code (through its adapter, game/native_draw/) hands over instead of VU1 packets. It knows
   nothing of King's Field IV: meshes made from PS2 data blocks, cameras, and draws that carry what VU1's
   fixed block carries. port/render/render.cpp is it; the native GS (port/gs/native/gsn_native_draw.inc)
   draws what it holds.

   How a draw travels (hosted mode, NATIVE_DRAW.md 3.3):
   1. On the game's thread, at the call site, while the game builds its packet chain: rd_mesh_from_block
      for each data block the draw would have sent (cached: a block is parsed once per gen), rd_camera
      for the camera it is seen by, then rd_draw, which copies the draw into this frame's side buffer and
      returns a 64-bit handle.
   2. The adapter queues the handle in the chain as a GS register write to PORT_GS_REG_NATIVE_DRAW
      (RD_ND_DRAW), at the very place of the packets it replaces.
   3. The native GS meets that write in order with everything else of the frame (on its own thread, a
      frame later), takes the draw back (render_internal.hpp, rd_fetch), and draws it into its current
      target and Z buffer with its own GS state, textures and pipelines; the vertex program runs on the
      GPU (shaders/nd_000.vert).
   The side buffer is a ring of frames (128); a handle names its frame. The consumer meets a mark at each
   frame's start (RD_ND_FRAME) and says so (rd_consumer_frame), and a slot is not reused before it is two
   frames past it; a draw met after its slot was reused all the same is not drawn, and counted
   (rd_stats.lost).

   Only the game's thread calls the rd_* functions here (save rd_stats). They do nothing that the
   game can see: no game RAM is written, and nothing of it is in a save state. */
#ifndef PORT_RENDER_H
#define PORT_RENDER_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Meshes ----------------------------------------------------------------------------------- */
/* A mesh: one PS2 data block (the bytes a DMA REF sends to VIF1: UNPACKs into VU1 memory and the
   MSCAL/MSCNTs that draw them), parsed into batches: each batch is what one VU1 run draws (its GIF
   tag, the A+D registers it kicks, its vertices as VU1 memory has them). 0: none (unsupported). */
typedef uint32_t RdMesh;

typedef enum {
    RD_VF_PACKED16 = 1,  /* program 0x000's: 4 qwords a vertex (V4-16 position, normal, ST, colour) */
    RD_VF_FLOAT_SKIN = 2 /* program 0x280's: float vertices (not parsed yet: P2) */
} RdVertexFormat;

/* gen RD_GEN_HASH: the block's bytes are hashed each call, so a block the game rewrote (Model_Set*,
   Wobble_ApplyWaveUvs ...) is parsed again without being told. Any other gen is the caller's count of
   the block's writes (rd_mesh_touch): cheaper, but only right if every writer is known. */
#define RD_GEN_HASH 0u
/* block: the block's bytes (its first qword is the VIF stream's first); key: what identifies it
   (its EE address); returns 0 when the block is not something it can draw (rd_mesh_why says why) */
RdMesh rd_mesh_from_block(const void *block, uint32_t qwords, uint32_t key, uint32_t gen, RdVertexFormat fmt);
/* the game wrote the block at key: the next rd_mesh_from_block parses it again (with a caller's gen) */
void rd_mesh_touch(uint32_t key);
/* why the last rd_mesh_from_block returned 0 (a constant string) */
const char *rd_mesh_why(void);
/* The VU1 program a mesh's first batch starts (its MSCAL's address), or RD_PROGRAM_CONTINUE (its
   first is an MSCNT: it goes on with the program and set-up of the mesh before it) */
#define RD_PROGRAM_CONTINUE 0xFFFFFFFFu
uint32_t rd_mesh_program(RdMesh mesh);

/* ---- Cameras ---------------------------------------------------------------------------------- */
/* What the game's camera was when a draw was queued (GameLoop_SetCameraMatrices). Matrices are VU
   layout: m[i] is the i-th qword (a column: v' = m[0] v.x + m[1] v.y + m[2] v.z + m[3] v.w). */
typedef struct {
    float world_screen[4][4]; /* VU1 0x00-0x03: what the vertex program multiplies by (world_screen_matrix) */
    float view[4][4];         /* world to view (world_view_matrix): x right, y down, z forward */
    float screen[4][4];       /* view to screen (view_screen_matrix: sceVu0ViewScreenMatrix's) */
    uint32_t mode;            /* GameLoop_SetCameraMatrices' mode: 0 the world, 1 its mirror image, 2 before the eye */
    uint32_t projection;      /* its projection: 0 the world's, 1 the reflection's */
} RdCamera;
/* this frame's camera index for c (the last one again if c is the same) */
uint32_t rd_camera(const RdCamera *c);

/* ---- Draws ------------------------------------------------------------------------------------ */
/* Kinds of draw (the identity's first part; also the --native-draw category it belongs to) */
enum {
    RD_KIND_MAP_RECORD = 1,
    RD_KIND_MAP_OBJECT,
    RD_KIND_ITEM,
    RD_KIND_GOLD,
    RD_KIND_ENTITY,
    RD_KIND_ARM,
    RD_KIND_EFFECT,
    RD_KIND_SHADOW,
};

/* One draw: a model part (or a map record's part) with its meshes, and what VU1's fixed block holds
   for it (docs/port/NATIVE_DRAW.md 1.2.9), by value. Hosted mode takes the GS state (ZBUF, TEST,
   ALPHA, the context ...) from the chain: the call site queues those registers as the PS2 path does. */
typedef struct {
    uint32_t kind;            /* RD_KIND_* */
    uint32_t id[3];           /* the rest of its identity, stable from frame to frame: (instance key, part,
                                 slot or record): what pictures between pair draws by (3.5) */
    uint32_t program;         /* the VU1 program whose maths it is drawn with: 0x000 (others: later) */
    uint32_t flags;           /* (none yet) */
    uint32_t camera;          /* rd_camera's index */
    uint32_t mesh_count;      /* (filled by rd_draw) */
    float model[4][4];        /* VU1 0x04-0x07: the part to the world (model x part) */
    float parent[4][4];       /* VU1 0x08-0x0B: the parent's (skinned; 0x280) */
    float light_dir[4][4];    /* VU1 0x0C-0x0F: three light directions (rows), divided by the model's scale */
    float light_col[4][4];    /* VU1 0x10-0x13: three light colours and the ambient */
    float fog[4];             /* VU1 0x3C: (0, 0, A, B): F = A + B / w, clamped 0..255 */
    uint64_t fogcol;          /* VU1 0x3D-0x3E's packet: FOGCOL */
    int32_t point_count;      /* VU1 0x40.w: point lights, up to 4 */
    uint32_t pad0;
    float point_pos[4][4];    /* VU1 0x41-0x44: positions (world), w the falloff */
    float point_col[4][4];    /* VU1 0x45-0x48: colours */
    float colour[3][4];       /* VU1 0x64-0x66: tint (skinned: scale and bias), effects' colour and TEX0 swap */
} RdDraw;

/* Copies d (and the meshes, which stay alive as long as the frame does) into this frame's side buffer;
   returns the handle to queue as PORT_GS_REG_NATIVE_DRAW's value (RD_ND_DRAW), 0 if it cannot */
uint64_t rd_draw(const RdDraw *d, const RdMesh *meshes, uint32_t count);

/* The start of a frame's packets (GameLoop_DrawFrame): the next slot of the side buffer. Returns the
   RD_ND_FRAME mark to queue at once, by which the consumer says how far it has got (rd_consumer_frame):
   a slot is not reused before the consumer is two frames past it (waiting for it if need be). */
uint64_t rd_begin_frame(void);
/* (the consumer, on its thread: it has met this RD_ND_FRAME mark) */
void rd_consumer_frame(uint64_t value);

/* ---- The chain's marks --------------------------------------------------------------------------
   PORT_GS_REG_NATIVE_DRAW (port/include/port/host.h): bits 56-63 of the value are one of these, the rest
   theirs. */
#define RD_ND_DRAW 0x01u       /* bits 0-55: rd_draw's handle */
#define RD_ND_ALT_BEGIN 0x02u  /* --native-draw-compare: the PS2 path's packets for the native draws before follow */
#define RD_ND_ALT_END 0x03u    /* ... and end here */
#define RD_ND_FRAME 0x04u      /* bits 0-23: a frame's serial (rd_begin_frame) */
#define RD_ND_COMMAND(value) ((uint32_t)((uint64_t)(value) >> 56))

/* ---- What it did (--stats, the overlay) ----------------------------------------------------------- */
typedef struct {
    uint64_t draws, meshes_parsed, mesh_hits, mesh_unsupported, hash_bytes;
    uint64_t lost;   /* draws met after their slot was reused */
    uint64_t meshes_held; /* meshes in the cache now */
    uint64_t waits, waits_given_up; /* rd_begin_frame waiting for the consumer (200 us each) */
} RdStats;
extern RdStats rd_stats;

#ifdef __cplusplus
}
#endif
#endif /* PORT_RENDER_H */
