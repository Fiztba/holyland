#version 450
#extension GL_GOOGLE_include_directive : require
// Native draw (port/gs/native/gsn_nd_pass.inc): a screen pass's corners in a picture between. gs.vert's
// vertices, moved as gs_replay.comp moves a paired 2D item of the PS2 path's, from the pass's record of the
// frame before (NdPassBetween): its colour t of the way from the frame before's, each rounded; the frame
// shown before blended in by A^t; the wobble grid's texels t of the way from the frame before's, in 12.4
// units rounded. Nothing else moves: the passes cover the screen where the frame has them.
#include "gs_common.h"

layout(location = 0) in vec2 a_pos;
layout(location = 1) in uint a_z;
layout(location = 2) in uvec4 a_color;
layout(location = 3) in vec3 a_stq;
layout(location = 4) in float a_fog;

layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) noperspective out vec3 v_stq;
layout(location = 2) noperspective out float v_fog;
layout(location = 3) noperspective out float v_z;

// a picture between's (gs_replay.h's Picture): zt.z is t, zt.w 1 - t
layout(set = 1, binding = 0, std430) readonly buffer Picture {
    vec4 move[3];
    vec4 mid[3];
    vec4 screen[2];
    vec4 zt;
} pic;

// gsn_nd_pass.inc's NdPassBetween
#define ND_PASS_RGBA 0x1u
#define ND_PASS_PREV 0x2u
#define ND_PASS_PAIRED 0x4u
#define ND_PASS_GRID 0x8u
layout(set = 2, binding = 0, std430) readonly buffer Pass {
    uint mode;
    uint prev_rgba;
    uint pad0, pad1;
    uint prev_uv[88];
    uint uv[88];
} ps;

void main() {
    float u = pic.zt.w;
    uvec4 c = a_color;
    if ((ps.mode & ND_PASS_RGBA) != 0u) {
        for (uint i = 0u; i < 4u; i++) {
            int ac = int((ps.prev_rgba >> (i * 8u)) & 0xFFu), bc = int(c[i]);
            c[i] = uint(bc + int(floor(float(ac - bc) * u + 0.5))) & 0xFFu;
        }
    }
    if ((ps.mode & ND_PASS_PREV) != 0u) {
        float vb = float(c.a), va = float(ps.prev_rgba >> 24);
        float base = (ps.mode & ND_PASS_PAIRED) != 0u ? vb + (va - vb) * u : vb;
        float a = base / 128.0;
        a = a <= 0.0 ? 0.0 : a >= 1.0 ? 1.0 : pow(a, pic.zt.z);
        c.a = uint(floor(a * 128.0 + 0.5)) & 0xFFu;
    }
    vec3 stq = a_stq;
    if ((ps.mode & ND_PASS_GRID) != 0u) {
        // (the corner's point of the grid by its place: 64 pixels apart from the frame's corner)
        int j = clamp(int(floor(a_pos.x / 64.0 + 0.5)), 0, 10), i = clamp(int(floor(a_pos.y / 64.0 + 0.5)), 0, 7);
        uint was = ps.prev_uv[i * 11 + j], now = ps.uv[i * 11 + j];
        vec2 vb = vec2(float(now & 0x3FFFu), float(now >> 16)) / 16.0;
        vec2 va = vec2(float(was & 0x3FFFu), float(was >> 16)) / 16.0;
        vec2 moved = floor((vb + (va - vb) * u) * 16.0 + 0.5) / 16.0;
        stq.xy = a_stq.xy + (moved - vb); // (and whatever the frame's corner was moved by: the 2D snap)
    }

    vec2 ndc = (a_pos + 0.5) * pc.inv_size * 2.0 - 1.0;
    float z = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(a_z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, z, 1.0);
    v_z = float(a_z);
    v_color = vec4(c);
    v_stq = stq;
    v_fog = a_fog;
}
