// ---- Native draw: VU1's program 0x5A0, the shadows (P5-1) ----------------------------------------------
// Included at the end of gsn_native_draw.inc. Program 0x5A0 (port/platform/vu1_hle_5a0.c is its
// specification) draws the pool models' silhouettes (Entity_DrawPoolModels: game/native_draw/nd_shadows.c)
// from program 0x000's own blocks. Per vertex it is 0x000's maths less the lights and the fog:
//   the set-up's vertex matrix (VU1 0x00 by 0x04, MULAx ACC ... MADDw: 0x000's; vf01-vf04, not stored);
//   c = M * position; q = 1 / c.w; X, Y, Z = FTOI4(c.xyz * q); the same guard band and Z clip, the same ADC
//   (the kick's clip and the two before it, the data's from the normal's w, ORed into XYZF2's w);
//   S, T, Q = ST * q, ST's z 1.0 for every vertex but the last two (which keep the data's);
//   the colour (RGBAQ) 0, the fog clamped to 0..vf19.w = 0: 0;
//   at an MSCAL it kicks FOGCOL (VU1 0x3D), as 0x000's set-up does; it kicks no A+D packet.
// So it is drawn by nd_000.vert and planned by nd_plan, from a batch the parser stores as 0x000 would
// draw it to the same result (render.cpp, take_batch's silhouette: no A+D, each vertex's colour qword 0,
// ST's z 4096 but the last two's) and a record with no lights, no point lights and no fog: every term the
// lights add is then an exact 0 (0 times a finite number), the colour 0 by the vertex colour of 0, and the
// fog clamp(0 + 0 q, 0, 255) = 0, as 0x5A0's clamp to 0. The oracle (kf4-gsreplay --nd-oracle) checks it
// against the program's own kicks, lane by lane, as for 0x000.

// A draw's record of program 0x5A0: nd_const's (the vertex matrix, the same set-up), then what 0x5A0 does
// not have taken to 0
void nd5a0_const(NdConst &k) {
    memset(k.light_dir, 0, sizeof(k.light_dir));
    memset(k.light_col, 0, sizeof(k.light_col));
    memset(k.fog, 0, sizeof(k.fog));
    memset(k.point_pos, 0, sizeof(k.point_pos));
    memset(k.point_col, 0, sizeof(k.point_col));
    k.info[0] = 0;
}

} // namespace (gsn.cpp's: its C entry points below are outside it)
#include "gsn_nd_oracle.h"
extern "C" int gsn_nd_oracle_5a0_vertices(const void *vu_data, uint32_t top, uint32_t mscal, int32_t (*out)[16],
                                          uint32_t max, const char **why) {
    RdMeshData mesh;
    *why = "";
    if (!rd_batch_5a0_from_vu(static_cast<const uint32_t(*)[4]>(vu_data), top, mscal ? 0x5A0 : RD_PROGRAM_CONTINUE, mesh)) {
        *why = rd_mesh_why();
        return 0;
    }
    const RdBatch &b = mesh.batches[0];
    if (b.count > max) {
        *why = "more vertices than the caller has room for";
        return 0;
    }
    memcpy(out, mesh.vertices.data() + size_t(b.first) * 16, size_t(b.count) * 64);
    return int(b.count);
}
namespace {
