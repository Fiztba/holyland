#version 450
#extension GL_GOOGLE_include_directive : require
// Native draw (port/gs/native/gsn_nd_280.inc): VU1's program 0x280, the skinned models' (enemies, the arm,
// the held weapon), as a vertex shader: nd_000.vert's sibling, with the same outputs, so every fragment
// shader of the native GS draws it. port/platform/vu1_hle_280.c is the specification. Per vertex of a
// batch (one VU1 run):
//   the vertex's set s (its ST's w: 0 the part's matrices, else its parent's: rigid skinning, no weights);
//   c = vertex matrix[s] * position; q = 1 / c.w; X, Y, Z = FTOI4(c.xyz * q);
//   fog = clamp(fog.z + fog.w * q, 0, 255); dots = max(L[s] * normal, 0);
//   u = colour * scale + bias for vertex 0, then for vertex k its own alpha but the RGB of the vertex
//   before it carried on: u.rgb = u_prev.rgb * scale + bias (the program's pipeline loads only the alpha
//   after the first vertex); colour = min((C * dots + ambient) * u, 255) (alpha: u's), FTOI0;
//   each point light (in the vertex's set; vertex 0 takes the batch tag's: the parser's qword 4) adds
//   colour * max(1 - |light - position|^2 * light A's w, 0), clamped at 255;
//   S, T, Q = ST * q.
// The second pass (the "phong" shine: gsn_nd_280.inc draws it when the batch has it): the same XYZF2,
// ST = ((sphere matrix[s] * normal).xy + 1) / 2 and Q 1, RGBA (255, 255, 255, vertex 1's alpha).
// VU1's arithmetic as in nd_000.vert (the vu_* helpers below are its: each operation rounded toward zero,
// in the program's order; the division exact). The constants come as VU1's set-up leaves them
// (gsn_nd_280.inc, nd280_const). Which triangles are drawn, and whether a batch draws at all (VU1 ends
// the run when every vertex is clipped), are the CPU's (nd280_plan).
// The arena (set 2, binding 0) holds a vertex in eight qwords (render.cpp, take_batch_280): position,
// normal, ST, colour as VU1 memory had them (floats), then (k, the point lights' set, what XYZF2's w is
// ORed with, n). A corner's fog input is -1 less its vertex's first qword / 4 in the arena; its colour's RGB the
// kick's number less its own, in vertices (bits 0-5), IIP (bit 6), the draw's record (bits 7-21), the
// context whose XYOFFSET (bit 22) and the pass (bit 23).
// Not here yet: pictures between (P2-5); with nothing moving the Picture (set 1) is not read.
#include "gs_common.h"

layout(location = 2) in uvec4 a_corner;
layout(location = 4) in float a_vertex;

layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) noperspective out vec3 v_stq;
layout(location = 2) noperspective out float v_fog;
layout(location = 3) noperspective out float v_z;

// (bound by nd_bind as for nd_000.vert; not read yet)
layout(set = 1, binding = 0, std430) readonly buffer Picture {
    vec4 move[3];
    vec4 mid[3];
    vec4 screen[2];
    vec4 zt;
} pic;
layout(set = 2, binding = 0, std430) readonly buffer Mesh {
    ivec4 q[];
} mesh;
// the draws (gsn_nd_280.inc, Nd280Const): matrices a VU qword a column; [0] set A, [1] set B
struct Draw {
    mat4 vertex[2];     // VU1 0x14-0x17, 0x1C-0x1F: view-screen by the part's matrix, by its parent's
    mat4 light_dir[2];  // VU1 0x18-0x1A, 0x20-0x22: the light directions by them (a column each)
    mat4 light_col;     // vf08-vf11: the light colours, then the ambient
    mat4 sphere[2];     // VU1 0x2C-0x2E, 0x30-0x32: the turns for the sphere pass
    vec4 fog;           // VU1 0x3C
    vec4 scale;         // VU1 0x72 (0x64): the colour scale
    vec4 bias;          // VU1 0x73 (0x65): and bias
    vec4 point_pos[8];  // VU1 0x4A-0x4D (set A's space; w the falloff), 0x53-0x56 (set B's)
    vec4 point_col[4];  // VU1 0x4E-0x51
    ivec4 info;         // the point lights, ND_C_* flags
    ivec4 offset;       // XYOFFSET_1's X and Y, XYOFFSET_2's (12.4)
};
layout(set = 2, binding = 1, std430) readonly buffer Draws {
    Draw draws[];
};

#ifdef ND_ORACLE
// the vertex oracle (nd_280_oracle.vert: gsn_nd_280.inc's oracle): what each corner gives the GS, two
// qwords a corner: X, Y and Z as worked out below, XYZF2's w; RGBA, S, T, Q
layout(set = 2, binding = 2, std430) writeonly buffer Oracle {
    uvec4 o[];
} oracle;
#endif

Draw d;

// ---- VU1's arithmetic (nd_000.vert's helpers) ----
// FTOI: toward zero; from 2^31 on, the largest integer with the float's sign (VU1's saturation: a vertex
// far past the guard band, never drawn, then has VU1's X, Y and Z too)
int ftoi(float v) {
    return v >= 2147483648.0 ? 0x7FFFFFFF : v <= -2147483648.0 ? int(0x80000000u) : int(v);
}

float toward_zero(float r) {
    return r == 0.0 ? r : uintBitsToFloat(floatBitsToUint(r) - 1u);
}

float vu_mul(float a, float b) {
    precise float r = a * b;
    precise float e = fma(a, b, -r);
    return e != 0.0 && (e < 0.0) != (r < 0.0) ? toward_zero(r) : r;
}

float vu_add(float a, float b) {
    precise float s = a + b;
    precise float bb = s - a;
    precise float e = (a - (s - bb)) + (b - bb);
    return e != 0.0 && (e < 0.0) != (s < 0.0) ? toward_zero(s) : s;
}

vec4 vu_add4(vec4 a, vec4 b) {
    return vec4(vu_add(a.x, b.x), vu_add(a.y, b.y), vu_add(a.z, b.z), vu_add(a.w, b.w));
}

vec4 vu_mul4(vec4 a, vec4 b) {
    return vec4(vu_mul(a.x, b.x), vu_mul(a.y, b.y), vu_mul(a.z, b.z), vu_mul(a.w, b.w));
}

vec4 vu_transform(mat4 m, vec4 v) {
    vec4 acc = vu_mul4(m[0], vec4(v.x));
    acc = vu_add4(acc, vu_mul4(m[1], vec4(v.y)));
    acc = vu_add4(acc, vu_mul4(m[2], vec4(v.z)));
    return vu_add4(acc, vu_mul4(m[3], vec4(v.w)));
}

// m's first three columns by v.xyz (MULAx; MADDAy; MADDz)
vec4 vu_transform3(mat4 m, vec3 v) {
    vec4 acc = vu_mul4(m[0], vec4(v.x));
    acc = vu_add4(acc, vu_mul4(m[1], vec4(v.y)));
    return vu_add4(acc, vu_mul4(m[2], vec4(v.z)));
}

float vu_recip(float w) {
    if ((floatBitsToUint(w) & 0x7F800000u) == 0u) {
        return uintBitsToFloat((floatBitsToUint(w) & 0x80000000u) | 0x7FFFFFFFu);
    }
    float aw = abs(w);
    precise float r = 1.0 / aw;
    precise float e = fma(-aw, r, 1.0);
    r = fma(r, e, r);
    for (int i = 0; i < 2; i++) {
        float up = uintBitsToFloat(floatBitsToUint(r) + 1u);
        if (fma(-aw, r, 1.0) < 0.0) {
            r = uintBitsToFloat(floatBitsToUint(r) - 1u);
        } else if (fma(-aw, up, 1.0) >= 0.0) {
            r = up;
        }
    }
    return w < 0.0 ? -r : r;
}

// ---- Program 0x280 ----
vec4 qf(uint slot, uint i) {
    return intBitsToFloat(mesh.q[slot * 4u + i]);
}

// The vertex's set: its ST's w (the low 16 bits, as ILW loads them)
int vertex_set(uint slot) {
    return (uint(mesh.q[slot * 4u + 2u].w) & 0xFFFFu) != 0u ? 1 : 0;
}

// u of the vertex in slot, k its number in the batch: vertex 0's colour by the scale and bias, then the
// RGB carried on k times, with the vertex's own alpha
vec4 colour_u(uint slot, int k) {
    vec4 u = vu_add4(vu_mul4(qf(slot - 2u * uint(k), 3u), d.scale), d.bias);
    for (int j = 1; j <= k; j++) {
        u.xyz = vec3(vu_add(vu_mul(u.x, d.scale.x), d.bias.x), vu_add(vu_mul(u.y, d.scale.y), d.bias.y),
                     vu_add(vu_mul(u.z, d.scale.z), d.bias.z));
    }
    u.w = vu_add(vu_mul(qf(slot, 3u).w, d.scale.w), d.bias.w);
    return u;
}

// The vertex's colour as VU1 leaves it in the strip (the lights; the point lights' passes)
uvec4 lit_colour(uint slot) {
    ivec4 X = mesh.q[slot * 4u + 4u];
    int s = vertex_set(slot);
    vec4 pos = qf(slot, 0u), n = qf(slot, 1u);
    vec4 dots = max(vu_transform3(d.light_dir[s], n.xyz), vec4(0.0));
    vec4 acc = vu_mul4(d.light_col[0], vec4(dots.x));
    acc = vu_add4(acc, vu_mul4(d.light_col[1], vec4(dots.y)));
    vec4 lit = vu_add4(acc, vu_mul4(d.light_col[2], vec4(dots.z)));
    lit = vec4(vu_add(lit.x, d.light_col[3].x), vu_add(lit.y, d.light_col[3].y), vu_add(lit.z, d.light_col[3].z), 1.0);
    vec4 last = min(vu_mul4(lit, colour_u(slot, X.x)), vec4(255.0));
    ivec4 c = ivec4(ftoi(last.x), ftoi(last.y), ftoi(last.z), ftoi(last.w));
    int ps = X.y != 0 ? 4 : 0;
    for (int l = 0; l < d.info.x && l < 4; l++) {
        vec4 lp = d.point_pos[ps + l];
        vec3 dd = vec3(vu_add(lp.x, -pos.x), vu_add(lp.y, -pos.y), vu_add(lp.z, -pos.z));
        float p = vu_add(vu_add(vu_mul(dd.x, dd.x), vu_mul(dd.y, dd.y)), vu_mul(dd.z, dd.z));
        float att = vu_add(1.0, -vu_mul(p, d.point_pos[l].w)); // (the falloff: set A's light's)
        vec3 a = max(vec3(vu_mul(d.point_col[l].x, att), vu_mul(d.point_col[l].y, att), vu_mul(d.point_col[l].z, att)),
                     vec3(0.0));
        vec3 sum = vec3(vu_add(float(c.x), a.x), vu_add(float(c.y), a.y), vu_add(float(c.z), a.z));
        sum = min(sum, vec3(255.0));
        c.xyz = ivec3(ftoi(sum.x), ftoi(sum.y), ftoi(sum.z));
    }
    return uvec4(c) & 0xFFu;
}

void main() {
    uint slot = uint(-a_vertex) - 1u; // (-1 less it: nd_corner_vertex)
    uint corner = a_corner.r | a_corner.g << 8 | a_corner.b << 16;
    uint kick = slot + 2u * (corner & 0x3Fu);
    bool sphere = (corner & 0x800000u) != 0u;
    d = draws[(corner >> 7) & 0x7FFFu];
    ivec2 offset = (corner & 0x400000u) != 0u ? d.offset.zw : d.offset.xy;
    ivec4 X = mesh.q[slot * 4u + 4u];
    int s = vertex_set(slot);

    // the place: XYZF2 (the same in both passes)
    vec4 c = vu_transform(d.vertex[s], qf(slot, 0u));
    float q = vu_recip(c.w);
    int xi = ftoi(vu_mul(c.x, q) * 16.0), yi = ftoi(vu_mul(c.y, q) * 16.0), zi = ftoi(vu_mul(c.z, q) * 16.0);
    uint z = uint(zi >> 4) & 0xFFFFFFu;
    float fogf = max(min(vu_add(d.fog.z, vu_mul(d.fog.w, q)), 255.0), 0.0);
    uint w16 = (uint(ftoi(fogf * 16.0)) | uint(X.z)) & 0xFFFFu;
    float fog = float((w16 >> 4) & 0xFFu);
    vec2 xy = vec2(float(int(uint(xi) & 0xFFFFu) - offset.x), float(int(uint(yi) & 0xFFFFu) - offset.y)) / 16.0;

    vec3 stq;
    uvec4 rgba;
    if (!sphere) {
        vec4 st = qf(slot, 2u);
        stq = vec3(vu_mul(st.x, q), vu_mul(st.y, q), vu_mul(st.z, q));
        rgba = lit_colour((corner & 0x40u) != 0u ? slot : kick);
    } else {
        // ((Ms * normal).xy + 1 * 1) * 0.5; Q 1; white, with vertex 1's alpha (its u's: lit.w is 1)
        vec4 acc = vu_transform3(d.sphere[s], qf(slot, 1u).xyz);
        stq = vec3(vu_mul(vu_add(acc.x, 1.0), 0.5), vu_mul(vu_add(acc.y, 1.0), 0.5), 1.0);
        uint one = slot - 2u * uint(X.x) + 2u;
        float a1 = min(vu_add(vu_mul(qf(one, 3u).w, d.scale.w), d.bias.w), 255.0);
        rgba = uvec4(255u, 255u, 255u, uint(ftoi(a1)) & 0xFFu);
    }

    // as gs.vert
    vec2 ndc = (xy + 0.5) * pc.inv_size * 2.0 - 1.0;
    float depth = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, depth, 1.0);
    v_z = float(z);
    v_color = vec4(rgba);
    v_stq = stq;
    v_fog = fog;
#ifdef ND_ORACLE
    oracle.o[gl_VertexIndex * 2] = uvec4(uint(xi), uint(yi), z, w16);
    oracle.o[gl_VertexIndex * 2 + 1] = uvec4(rgba.r | rgba.g << 8 | rgba.b << 16 | rgba.a << 24, floatBitsToUint(v_stq));
#endif
}
