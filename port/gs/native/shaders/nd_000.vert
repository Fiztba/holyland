#version 450
#extension GL_GOOGLE_include_directive : require
// Native draw (port/gs/native/gsn_native_draw.inc): VU1's program 0x000 as a vertex shader. Each corner of
// a batch's triangles is made from the mesh's vertex as VU1 memory had it, and the draw's constants (what
// VU1's fixed block held), into what gs.vert takes from a GS vertex: the same outputs, so every fragment
// shader of the native GS draws it. port/platform/vu1_hle.c is the specification:
//   c = (world_screen * model) * position; q = 1 / c.w; X, Y, Z = FTOI4(c.xyz * q) (12.4 and Z);
//   fog = clamp(fog.z + fog.w * q, 0, 255); dots = max(L' * normal, 0) (L' = the light directions by the
//   model's rotation); colour = min((C * dots + ambient) * vertex colour, 255) (alpha: the vertex's);
//   then each point light adds colour * max(1 - |light - position|^2 * light.w, 0), clamped at 255;
//   S, T, Q = ST * q (ST's z is 1: Q is q).
// The GS's numbers: X and Y as XYZF2 has them (the integer's low 16 bits, less XYOFFSET), Z an integer
// (Z / 16 of FTOI4's, as the GIF's PACKED XYZF2 takes it), RGBA bytes, fog F as an integer.
// Floats are the GPU's (rounded to nearest) where VU1 rounds toward zero: a place can come out a
// sixteenth of a pixel apart, a colour 1 apart.
// Which triangles are drawn (the data's ADC bits, and VU1's clipping) is the CPU's (gsn_native_draw.inc,
// nd_plan): a corner's fog input names its vertex (bits 0-7) and the kick's (8-15), whose colour a flat
// triangle has.
#include "gs_common.h"

layout(location = 4) in float a_code;

layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) noperspective out vec3 v_stq;
layout(location = 2) noperspective out float v_fog;
layout(location = 3) noperspective out float v_z;

// a picture between's (gs_replay.h's Picture; with nothing moving, a buffer of zeros)
layout(set = 1, binding = 0, std430) readonly buffer Picture {
    vec4 move[3]; // the frame's view to the picture's: rows of the rotation less the identity, each with its translation
} pic;
// the mesh: four qwords a vertex (position, normal, ST, colour), as VU1 memory had them
layout(set = 2, binding = 0, std430) readonly buffer Mesh {
    ivec4 q[];
} mesh;
// the draw (gsn_native_draw.inc, NdConst): matrices a VU qword a column
layout(set = 2, binding = 1, std140) uniform Draw {
    mat4 world_screen; // VU1 0x00
    mat4 model;        // VU1 0x04: the part to the world
    mat4 view;         // world to view (pictures between)
    mat4 screen;       // view to screen (pictures between)
    mat4 light_dir;    // VU1 0x0C: the light directions, a column each
    mat4 light_col;    // VU1 0x10: the light colours, then the ambient
    vec4 fog;          // VU1 0x3C
    vec4 point_pos[4]; // in the model's space, w the falloff
    vec4 point_col[4];
    ivec4 info;        // the point lights, XYOFFSET X and Y (12.4), ND_C_*
    ivec4 info2;       // the batch's first vertex
} d;

#define ND_C_MOVING 1
#define ND_C_GOURAUD 2

// FTOI: toward zero, kept within what an int holds
int ftoi(float v) {
    return int(clamp(v, -2147483520.0, 2147483520.0));
}

// The vertex's colour as VU1 leaves it in RGBAQ (the lights; the point lights' passes)
uvec4 lit_colour(uint k) {
    uint at = uint(d.info2.x) + k;
    ivec4 P = mesh.q[at * 4u], N = mesh.q[at * 4u + 1u], C = mesh.q[at * 4u + 3u];
    vec3 n = vec3(N.xyz) / 4096.0;
    mat4 lm = d.light_dir * d.model;
    vec4 dots = max(lm[0] * n.x + lm[1] * n.y + lm[2] * n.z, vec4(0.0));
    vec4 lit = d.light_col[0] * dots.x + d.light_col[1] * dots.y + d.light_col[2] * dots.z;
    lit = vec4(lit.xyz + d.light_col[3].xyz, 1.0);
    ivec4 c = ivec4(min(lit * vec4(C), vec4(255.0)));
    vec3 pf = vec3(P.xyz);
    for (int l = 0; l < d.info.x && l < 4; l++) {
        vec3 dd = d.point_pos[l].xyz - pf;
        float p = dd.x * dd.x + dd.y * dd.y + dd.z * dd.z;
        float att = 1.0 - p * d.point_pos[l].w;
        vec3 a = max(d.point_col[l].xyz * att, vec3(0.0));
        c.xyz = ivec3(min(vec3(c.xyz) + a, vec3(255.0)));
    }
    return uvec4(c) & 0xFFu;
}

void main() {
    uint code = uint(a_code);
    uint k = code & 0xFFu, kick = (code >> 8) & 0xFFu;
    uint at = uint(d.info2.x) + k;
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
        c = (d.world_screen * d.model) * pos;
    }
    float q = 1.0 / c.w;
    vec3 proj = c.xyz * q;
    float fogf = max(min(d.fog.z + d.fog.w * q, 255.0), 0.0);

    // XYZF2 as the GS takes it
    int xi = ftoi(proj.x * 16.0), yi = ftoi(proj.y * 16.0), zi = ftoi(proj.z * 16.0);
    uint z = uint(zi >> 4) & 0xFFFFFFu;
    uint w16 = (uint(ftoi(fogf * 16.0)) | uint(N.w)) & 0xFFFFu;
    float fog = float((w16 >> 4) & 0xFFu);
    vec2 xy = vec2(float(int(uint(xi) & 0xFFFFu) - d.info.y), float(int(uint(yi) & 0xFFFFu) - d.info.z)) / 16.0;

    uvec4 rgba = lit_colour((d.info.w & ND_C_GOURAUD) != 0 ? k : kick);
    vec3 st = vec3(T.xyz) / 4096.0;

    // as gs.vert
    vec2 ndc = (xy + 0.5) * pc.inv_size * 2.0 - 1.0;
    float depth = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, depth, 1.0);
    v_z = float(z);
    v_color = vec4(rgba);
    v_stq = st * q;
    v_fog = fog;
}
