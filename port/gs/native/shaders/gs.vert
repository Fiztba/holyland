#version 450
#extension GL_GOOGLE_include_directive : require
// The native GS's vertices (port/gs/native/gsn.cpp): already in frame buffer pixels (the GS's X and Y
// less XYOFFSET), one vertex per triangle corner. A GS pixel is drawn when its integer point is inside
// a primitive; Vulkan tests the pixel's middle, so every vertex moves half a pixel.
#include "gs_common.h"

layout(location = 0) in vec2 a_pos;
layout(location = 1) in uint a_z;
layout(location = 2) in uvec4 a_color;
layout(location = 3) in vec3 a_stq;
layout(location = 4) in float a_fog;

layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) noperspective out vec3 v_stq;
layout(location = 2) noperspective out float v_fog;
layout(location = 3) noperspective out float v_z; // (gs_zclamp.frag's)

void main() {
    vec2 ndc = (a_pos + 0.5) * pc.inv_size * 2.0 - 1.0;
    // the GS keeps Z within what the Z buffer's format holds (with a vertex beyond it, per pixel:
    // gs_zclamp.frag writes the depth, and the vertices' is left where nothing clips it)
    float z = (pc.misc & GSN_M_ZCLAMP) != 0u ? 0.0 : float(min(a_z, pc.zmax)) * pc.zscale;
    gl_Position = vec4(ndc, z, 1.0);
    v_z = float(a_z);
    v_color = vec4(a_color);
    v_stq = a_stq;
    v_fog = a_fog;
}
