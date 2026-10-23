// The renderer core's insides, for the code that draws what it holds (the native GS in hosted mode:
// port/gs/native/gsn_native_draw.inc). C++ only; the game's side uses port/include/port/render.h.
#pragma once
#include <stdint.h>

#include <memory>
#include <vector>

extern "C" {
#include "port/render.h"
}

// One VU1 run of a mesh: what it kicks (the A+D packet at TOP+0xA0, then the strip) and its vertices
struct RdBatch {
    uint64_t tag[2];                 // the strip's GIF tag (NLOOP, PRE and PRIM, FLG, NREG, REGS)
    std::vector<uint64_t> ad;        // the A+D packet's writes kicked before it: register, value, register ...
    uint32_t start;                  // the MSCAL's address that starts it, or RD_PROGRAM_CONTINUE (an MSCNT)
    uint32_t first;                  // its first vertex in RdMeshData::vertices
    uint32_t count;                  // its vertices (the tag's NLOOP)
    // the triangles the GS makes of the strip (or list, or fan), the kicks the data's ADC bits leave out
    // already left out: three vertex numbers each (within the batch), the last the kick's
    std::vector<uint8_t> triangles;
    uint32_t alpha_lo, alpha_hi;     // the least and most alpha of its vertex colours (clamped to 0..255)
};

// A mesh: a block parsed. Vertices are as VU1 memory has them after VIF1's UNPACK: four qwords each
// (program 0x000: position, normal, ST, colour as 32-bit integers), 16 words a vertex.
struct RdMeshData {
    RdMesh id;
    uint32_t key, gen;
    std::vector<RdBatch> batches;
    std::vector<int32_t> vertices;
};

// The draw a PORT_GS_REG_NATIVE_DRAW value names, its camera and meshes, if its frame's slot still
// holds it (any thread)
bool rd_fetch(uint64_t value, RdDraw &draw, RdCamera &camera, std::vector<std::shared_ptr<const RdMeshData>> &meshes);
