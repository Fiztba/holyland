#version 450
#extension GL_GOOGLE_include_directive : require
// Native draw (port/gs/native/gsn_nd_pass.inc): a screen pass's corners in a picture between. gs.vert's
// vertices, moved as gs_replay.comp moves a paired 2D item of the PS2 path's, from the pass's record of the
// frame before (NdPassBetween): its colour t of the way from the frame before's, each rounded; the frame
// shown before blended in by A^t; the wobble grid's texels t of the way from the frame before's, in 12.4
// units rounded; the view-turn shift's strips t of the way from the frame before's places. Where flush_batch's
// flat_2d moved a triangle out to the edges of its GS pixels (at a scale, with MSAA), its corners are made
// again as gs_replay_corners.comp makes the PS2 path's, from the picture's places and texels (NdPassTris).
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
#define ND_PASS_SHIFT 0x10u
layout(set = 2, binding = 0, std430) readonly buffer Pass {
    uint mode;
    uint prev_rgba;
    uint pad0, pad1;
    uint prev_uv[88];
    uint uv[88];
    uint cells[4]; // (ND_PASS_GRID: the wobble's cells frame_interp.c pairs and moves, i * 10 + j)
} ps;

// gsn_nd_pass.inc's NdPassTris (nd_pass_bind_tris): the XYOFFSET of the corners, the place of the wobble
// grid's first point, and each triangle of the draw's run: the wobble's (bit 31: flat_2d moved it), its
// corners' points of the grid, 7 bits each in the order of its corners; the view shift's (bit 31), its strip
// (bits 0-4), which of the sprite's two triangles (bit 5), and whether flat_2d moved it (bit 30)
layout(set = 2, binding = 2, std140) uniform Tris {
    vec2 origin;
    uint offset, pad;
    uvec4 tri[64];
} tris;

// gs_replay_corners.comp's flat_2d (the same arithmetic in the same order): o, one of the triangle's
// corners p[] with their texels at[], out to the edges of the GS pixels it covers
vec3 flat_2d(vec2 p[3], vec3 at[3], uint own) {
    float ax = p[1].x - p[0].x, ay = p[1].y - p[0].y;
    float bx = p[2].x - p[0].x, by = p[2].y - p[0].y;
    float det = ax * by - bx * ay;
    vec3 da = at[1] - at[0], db = at[2] - at[0];
    vec3 dx = (da * by - db * ay) / det, dy = (db * ax - da * bx) / det;
    vec2 o = p[own], n = ceil(o) - 0.5;
    return at[own] + dx * (n.x - o.x) + dy * (n.y - o.y);
}

// (the view shift) a strip's corner k at the picture's time: its X | Y << 16 and U | V << 16 (12.4), moved t of
// the way from the frame before's as gs_replay.comp moves a paired 2D item's (frame_interp.c's)
uvec2 shift_corner(uint strip, uint k, bool paired, float u) {
    uint xy = ps.uv[k], step = ps.uv[4], uvw = ps.uv[2u + k];
    int bx = int(((xy & 0xFFFFu) + step * strip) & 0xFFFFu), by = int(xy >> 16);
    uint bu = (uvw + step * strip) & 0x3FFFu, bv = (uvw >> 16) & 0x3FFFu;
    if (!paired) {
        return uvec2(uint(bx) | uint(by) << 16, bu | bv << 16);
    }
    uint pxy = ps.prev_uv[k], pstep = ps.prev_uv[4], puvw = ps.prev_uv[2u + k];
    int ax = int(((pxy & 0xFFFFu) + pstep * strip) & 0xFFFFu), ay = int(pxy >> 16);
    uint au = (puvw + pstep * strip) & 0x3FFFu, av = (puvw >> 16) & 0x3FFFu;
    int x = bx + int(floor(float(ax - bx) * u + 0.5)), y = by + int(floor(float(ay - by) * u + 0.5));
    float bs = float(bu) / 16.0, bt = float(bv) / 16.0;
    float ds = float(au) / 16.0 - bs, dt = float(av) / 16.0 - bt;
    float us = bs + ds * u, vt = bt + dt * u;
    uint uv = (uint(int(floor(us * 16.0 + 0.5))) & 0x3FFFu) | (uint(int(floor(vt * 16.0 + 0.5))) & 0x3FFFu) << 16;
    return uvec2((uint(x) & 0xFFFFu) | (uint(y) & 0xFFFFu) << 16, uv);
}

void main() {
    float u = pic.zt.w;
    uvec4 c = a_color;
    // (the corner's triangle in the draw's run, and which of its corners it is)
    uint tri = uint(gl_VertexIndex) / 3u, own = uint(gl_VertexIndex) % 3u;
    uint info = (ps.mode & (ND_PASS_GRID | ND_PASS_SHIFT)) != 0u && tri < 256u ? tris.tri[tri >> 2u][tri & 3u] : 0u;
    // (the wobble: a cell frame_interp.c does not pair is drawn as the frame has it, colour and texels; the
    // cell is its triangle's first point of the grid across and down)
    bool cell_moves = true;
    if ((ps.mode & ND_PASS_GRID) != 0u && (info & 0x80000000u) != 0u) {
        uint g0 = info & 0x7Fu, g1 = (info >> 7u) & 0x7Fu, g2 = (info >> 14u) & 0x7Fu;
        uint ci = min(g0 / 11u, min(g1 / 11u, g2 / 11u)), cj = min(g0 % 11u, min(g1 % 11u, g2 % 11u));
        uint cell = min(ci, 6u) * 10u + min(cj, 9u);
        cell_moves = ((ps.cells[cell >> 5u] >> (cell & 31u)) & 1u) != 0u;
        u = cell_moves ? u : 0.0;
    }
    if ((ps.mode & ND_PASS_RGBA) != 0u && cell_moves) {
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
    vec2 pos = a_pos;
    vec3 stq = a_stq;
    if ((ps.mode & ND_PASS_GRID) != 0u && (info & 0x80000000u) != 0u) {
        // A cell's triangle: its three points of the grid at their places, with their texels t of the way.
        // Where flat_2d moved it, its texels move by the picture's slopes, as gs_replay_corners.comp moves the
        // PS2 path's (the CPU's moved them by the frame's: a texel or so apart over the whole triangle at 4x).
        vec2 p[3];
        vec3 at[3];
        for (uint k = 0u; k < 3u; k++) {
            uint g = (info >> (k * 7u)) & 0x7Fu, gi = g / 11u, gj = g % 11u;
            uint was = ps.prev_uv[g], now = ps.uv[g];
            vec2 vb = vec2(float(now & 0x3FFFu), float(now >> 16)) / 16.0;
            vec2 va = vec2(float(was & 0x3FFFu), float(was >> 16)) / 16.0;
            p[k] = tris.origin + vec2(float(gj), float(gi)) * 64.0;
            at[k] = vec3(floor((vb + (va - vb) * u) * 16.0 + 0.5) / 16.0, a_stq.z);
        }
        stq = (info & 0x40000000u) != 0u ? flat_2d(p, at, own) : at[own];
    } else if ((ps.mode & ND_PASS_GRID) != 0u) {
        // (the corner's point of the grid by its place: 64 pixels apart from the frame's corner)
        int j = clamp(int(floor(a_pos.x / 64.0 + 0.5)), 0, 10), i = clamp(int(floor(a_pos.y / 64.0 + 0.5)), 0, 7);
        uint was = ps.prev_uv[i * 11 + j], now = ps.uv[i * 11 + j];
        vec2 vb = vec2(float(now & 0x3FFFu), float(now >> 16)) / 16.0;
        vec2 va = vec2(float(was & 0x3FFFu), float(was >> 16)) / 16.0;
        vec2 moved = floor((vb + (va - vb) * u) * 16.0 + 0.5) / 16.0;
        stq.xy = a_stq.xy + (moved - vb);
    } else if ((ps.mode & ND_PASS_SHIFT) != 0u && (info & 0x80000000u) != 0u) {
        // A strip of the view-turn shift: its sprite's two corners at the picture's time (when frame_interp.c
        // pairs it), the triangle's corners made of them as add_sprite makes them (corners 0, 1, 2 or 1, 3, 2:
        // 0 a's X and Y, 1 b's X a's Y, 2 a's X b's Y, 3 b's), each at its place less XYOFFSET, and moved out
        // by flat_2d where the frame's were
        uint strip = info & 0x1Fu;
        bool paired = ((ps.pad0 >> strip) & 1u) != 0u;
        uvec2 ka = shift_corner(strip, 0u, paired, u), kb = shift_corner(strip, 1u, paired, u);
        vec2 off = vec2(float(tris.offset & 0xFFFFu), float(tris.offset >> 16));
        vec2 pa = (vec2(float(ka.x & 0xFFFFu), float(ka.x >> 16)) - off) / 16.0;
        vec2 pb = (vec2(float(kb.x & 0xFFFFu), float(kb.x >> 16)) - off) / 16.0;
        vec2 sa = vec2(float(ka.y & 0x3FFFu), float((ka.y >> 16) & 0x3FFFu)) / 16.0;
        vec2 sb = vec2(float(kb.y & 0x3FFFu), float((kb.y >> 16) & 0x3FFFu)) / 16.0;
        uint second = (info >> 5u) & 1u;
        vec2 p[3];
        vec3 at[3];
        for (uint k = 0u; k < 3u; k++) {
            uint corner = second != 0u ? (k == 0u ? 1u : k == 1u ? 3u : 2u) : k;
            bool cx = corner == 0u || corner == 2u, cy = corner < 2u;
            p[k] = vec2(cx ? pa.x : pb.x, cy ? pa.y : pb.y);
            at[k] = vec3(cx ? sa.x : sb.x, cy ? sa.y : sb.y, a_stq.z);
        }
        pos = p[own];
        stq = at[own];
        if ((info & 0x40000000u) != 0u) {
            stq = flat_2d(p, at, own);
            pos = ceil(p[own]) - 0.5;
        }
    }

    vec2 ndc = (pos + 0.5) * pc.inv_size * 2.0 - 1.0;
    float z = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(a_z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, z, 1.0);
    v_z = float(a_z);
    v_color = vec4(c);
    v_stq = stq;
    v_fog = a_fog;
}
