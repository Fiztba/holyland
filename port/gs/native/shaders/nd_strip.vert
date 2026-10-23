#version 450
#extension GL_GOOGLE_include_directive : require
// Native draw (port/gs/native/gsn_nd_strip.inc): a strip the game made on the CPU (the weapon's trail), in a
// picture between. gs.vert's vertices, each corner also carrying where it was in the frame before: its place
// in S and T (frame buffer pixels, as a_pos) and its RGBA in Q's bits (the strip is not textured). Drawn t of
// the way from there, as gs_replay.comp moves a paired 2D item of the PS2 path's: X and Y in 12.4 units
// rounded, each colour rounded; a corner not paired has the same in both, and stays.
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

// a picture between's (gs_replay.h's Picture): zt.w is 1 - t
layout(set = 1, binding = 0, std430) readonly buffer Picture {
    vec4 move[3];
    vec4 mid[3];
    vec4 screen[2];
    vec4 zt;
} pic;

void main() {
    float u = pic.zt.w;
    vec2 pos = a_pos + floor((a_stq.xy - a_pos) * 16.0 * u + 0.5) / 16.0;
    uint was = floatBitsToUint(a_stq.z);
    vec4 before = vec4(float(was & 0xFFu), float((was >> 8) & 0xFFu), float((was >> 16) & 0xFFu), float(was >> 24));
    vec4 colour = vec4(a_color) + floor((before - vec4(a_color)) * u + 0.5);

    vec2 ndc = (pos + 0.5) * pc.inv_size * 2.0 - 1.0;
    float z = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(a_z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, z, 1.0);
    v_z = float(a_z);
    v_color = colour;
    v_stq = vec3(0.0, 0.0, 1.0);
    v_fog = a_fog;
}
