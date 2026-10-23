#version 450
#extension GL_GOOGLE_include_directive : require
// Native draw (port/gs/native/gsn_native_draw.inc): VU1's program 0x000 as a vertex shader. Each corner of
// a batch's triangles is made from the mesh's vertex as VU1 memory had it, and the draw's constants (what
// VU1's fixed block held, through VU1's set-up), into what gs.vert takes from a GS vertex: the same
// outputs, so every fragment shader of the native GS draws it. port/platform/vu1_hle.c is the
// specification:
//   c = vertex matrix * position; q = 1 / c.w; X, Y, Z = FTOI4(c.xyz * q) (12.4 and Z);
//   fog = clamp(fog.z + fog.w * q, 0, 255); dots = max(L' * normal, 0) (L' = the light directions by the
//   model's rotation); colour = min((C * dots + ambient) * vertex colour, 255) (alpha: the vertex's);
//   then each point light adds colour * max(1 - |light - position|^2 * light.w, 0), clamped at 255;
//   S, T, Q = ST * q (ST's z is 1: Q is q).
// The GS's numbers: X and Y as XYZF2 has them (the integer's low 16 bits, less XYOFFSET), Z an integer
// (Z / 16 of FTOI4's, as the GIF's PACKED XYZF2 takes it), RGBA bytes, fog F as an integer.
// VU1's arithmetic: every operation rounded toward zero, in the program's order (vu_* below: the GPU's
// result rounded to nearest, moved a unit toward zero where its exact error says it went past; the
// division made exact). The draw's constants come as VU1's set-up leaves them (gsn_native_draw.inc,
// nd_draw: the vertex matrix, the lights). Denormals are taken as the GPU has them (VU1 reads them as 0;
// the game's numbers do not come near them). In a picture between the camera moves and the place is
// made in plain floats.
// Which triangles are drawn (the data's ADC bits, and VU1's clipping) is the CPU's (gsn_native_draw.inc,
// nd_plan): a corner's fog input is its vertex's number in the arena, and its colour's RGB the kick's
// number less it (bits 0-5: a flat triangle has the kick's colour), IIP (bit 6) and its draw's record
// (bits 7-23).
#include "gs_common.h"

layout(location = 2) in uvec4 a_corner;
layout(location = 4) in float a_vertex;

layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) noperspective out vec3 v_stq;
layout(location = 2) noperspective out float v_fog;
layout(location = 3) noperspective out float v_z;

// a picture between's (gs_replay.h's Picture; with nothing moving, a buffer of zeros)
layout(set = 1, binding = 0, std430) readonly buffer Picture {
    vec4 move[3]; // the frame's view to the picture's: rows of the rotation less the identity, each with its translation
} pic;
// the meshes (the arena): four qwords a vertex (position, normal, ST, colour), as VU1 memory had them
layout(set = 2, binding = 0, std430) readonly buffer Mesh {
    ivec4 q[];
} mesh;
// the draws (gsn_native_draw.inc, NdConst), a record each: matrices a VU qword a column
struct Draw {
    mat4 vertex;       // VU1 0x14: the set-up's vertex matrix (0x00 by 0x04)
    mat4 model;        // VU1 0x04: the part to the world (pictures between)
    mat4 view;         // world to view (pictures between)
    mat4 screen;       // view to screen (pictures between)
    mat4 light_dir;    // vf05-vf07: the light directions by the part's rotation, a column each
    mat4 light_col;    // VU1 0x10: the light colours, then the ambient
    vec4 fog;          // VU1 0x3C
    vec4 point_pos[4]; // in the model's space, w the falloff
    vec4 point_col[4];
    ivec4 info;        // the point lights, XYOFFSET X and Y (12.4), ND_C_*
};
layout(set = 2, binding = 1, std430) readonly buffer Draws {
    Draw draws[];
};

#define ND_C_MOVING 1

Draw d;

// FTOI: toward zero, kept within what an int holds
int ftoi(float v) {
    return int(clamp(v, -2147483520.0, 2147483520.0));
}

// One unit of the last place toward zero
float toward_zero(float r) {
    return r == 0.0 ? r : uintBitsToFloat(floatBitsToUint(r) - 1u);
}

// a * b, rounded toward zero: the GPU's product, less a unit where it is past the exact one
float vu_mul(float a, float b) {
    precise float r = a * b;
    precise float e = fma(a, b, -r);
    return e != 0.0 && (e < 0.0) != (r < 0.0) ? toward_zero(r) : r;
}

// a + b, rounded toward zero (the sum's error by Knuth's two-sum)
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

// m v, as MULAx ACC; MADDAy; MADDAz; MADDw (each product rounded, then the sum)
vec4 vu_transform(mat4 m, vec4 v) {
    vec4 acc = vu_mul4(m[0], vec4(v.x));
    acc = vu_add4(acc, vu_mul4(m[1], vec4(v.y)));
    acc = vu_add4(acc, vu_mul4(m[2], vec4(v.z)));
    return vu_add4(acc, vu_mul4(m[3], vec4(v.w)));
}

// 1 / w rounded toward zero (DIV): the GPU's reciprocal made the nearest by a Newton step, then the
// largest whose product with |w| is not over 1; by 0 the largest float with w's sign
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

// The vertex's colour as VU1 leaves it in RGBAQ (the lights; the point lights' passes)
uvec4 lit_colour(uint at) {
    ivec4 P = mesh.q[at * 4u], N = mesh.q[at * 4u + 1u], C = mesh.q[at * 4u + 3u];
    vec3 n = vec3(N.xyz) / 4096.0; // (ITOF12: exact)
    // the light dots: MULAx ACC, l0, n.x; MADDAy; MADDz; floored at 0
    vec4 acc = vu_mul4(d.light_dir[0], vec4(n.x));
    acc = vu_add4(acc, vu_mul4(d.light_dir[1], vec4(n.y)));
    vec4 dots = max(vu_add4(acc, vu_mul4(d.light_dir[2], vec4(n.z))), vec4(0.0));
    // the colours by the dots, the ambient added; w 1
    acc = vu_mul4(d.light_col[0], vec4(dots.x));
    acc = vu_add4(acc, vu_mul4(d.light_col[1], vec4(dots.y)));
    vec4 lit = vu_add4(acc, vu_mul4(d.light_col[2], vec4(dots.z)));
    lit = vec4(vu_add(lit.x, d.light_col[3].x), vu_add(lit.y, d.light_col[3].y), vu_add(lit.z, d.light_col[3].z), 1.0);
    ivec4 c = ivec4(min(vu_mul4(lit, vec4(C)), vec4(255.0)));
    vec3 pf = vec3(P.xyz);
    for (int l = 0; l < d.info.x && l < 4; l++) {
        vec4 lp = d.point_pos[l];
        vec3 dd = vec3(vu_add(lp.x, -pf.x), vu_add(lp.y, -pf.y), vu_add(lp.z, -pf.z));
        float p = vu_add(vu_add(vu_mul(dd.x, dd.x), vu_mul(dd.y, dd.y)), vu_mul(dd.z, dd.z));
        float att = vu_add(1.0, -vu_mul(p, lp.w));
        vec3 a = max(vec3(vu_mul(d.point_col[l].x, att), vu_mul(d.point_col[l].y, att), vu_mul(d.point_col[l].z, att)),
                     vec3(0.0));
        vec3 sum = vec3(vu_add(float(c.x), a.x), vu_add(float(c.y), a.y), vu_add(float(c.z), a.z));
        c.xyz = ivec3(min(sum, vec3(255.0)));
    }
    return uvec4(c) & 0xFFu;
}

void main() {
    uint at = uint(a_vertex);
    uint corner = a_corner.r | a_corner.g << 8 | a_corner.b << 16;
    uint kick = at + (corner & 0x3Fu);
    d = draws[corner >> 7];
    ivec4 P = mesh.q[at * 4u], N = mesh.q[at * 4u + 1u], T = mesh.q[at * 4u + 2u];
    vec4 pos = vec4(P);
    vec4 c;
    if ((d.info.w & ND_C_MOVING) != 0) {
        // a picture between: seen from its camera (gs_replay.comp's move: m = v + (R - I) v + T)
        vec4 v = d.view * (d.model * pos);
        vec3 dm = vec3(dot(pic.move[0].xyz, v.xyz) + pic.move[0].w, dot(pic.move[1].xyz, v.xyz) + pic.move[1].w,
                       dot(pic.move[2].xyz, v.xyz) + pic.move[2].w);
        c = d.screen * vec4(v.xyz + dm, 1.0);
    } else {
        c = vu_transform(d.vertex, pos);
    }
    float q = vu_recip(c.w);
    vec3 proj = vec3(vu_mul(c.x, q), vu_mul(c.y, q), vu_mul(c.z, q));
    // the fog: ACC.w = 1 * fog.z, then + fog.w * q; clamped to 0..255
    float fogf = max(min(vu_add(d.fog.z, vu_mul(d.fog.w, q)), 255.0), 0.0);

    // XYZF2 as the GS takes it
    int xi = ftoi(proj.x * 16.0), yi = ftoi(proj.y * 16.0), zi = ftoi(proj.z * 16.0);
    uint z = uint(zi >> 4) & 0xFFFFFFu;
    uint w16 = (uint(ftoi(fogf * 16.0)) | uint(N.w)) & 0xFFFFu;
    float fog = float((w16 >> 4) & 0xFFu);
    vec2 xy = vec2(float(int(uint(xi) & 0xFFFFu) - d.info.y), float(int(uint(yi) & 0xFFFFu) - d.info.z)) / 16.0;

    uvec4 rgba = lit_colour((corner & 0x40u) != 0u ? at : kick);
    vec3 st = vec3(T.xyz) / 4096.0; // (ITOF12: exact)

    // as gs.vert
    vec2 ndc = (xy + 0.5) * pc.inv_size * 2.0 - 1.0;
    float depth = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, depth, 1.0);
    v_z = float(z);
    v_color = vec4(rgba);
    v_stq = vec3(vu_mul(st.x, q), vu_mul(st.y, q), vu_mul(st.z, q));
    v_fog = fog;
}
