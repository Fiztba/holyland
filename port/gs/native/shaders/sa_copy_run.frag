#version 450
#extension GL_GOOGLE_include_directive : require
// Shader blending in the render pass, the copy a run blends from made by the run's own triangles
// (gsn_standalone.inc, "The copy a run blends from, where the run draws"): drawn with the run's vertex shader,
// rasterized conservatively (every pixel a triangle touches, all its samples), the target's pixels into the
// pass's second colour attachment, as pass_copy.frag (single-sampled) and pass_resolve.frag (SA_MS: each channel
// the mean of its samples rounded half up) make them over a rectangle. The number of samples comes in
// pc.src_offset.x and the scale (255) in pc.lod_k, which the scene's vertex shaders do not read.
#include "gs_common.h"
#ifdef SA_MS
layout(input_attachment_index = 1, set = 0, binding = 5) uniform subpassInputMS u_target;
#else
layout(input_attachment_index = 0, set = 0, binding = 4) uniform subpassInput u_target;
#endif
layout(location = 1) out vec4 o_copy;

void main() {
#ifdef SA_MS
    uvec4 total = uvec4(0u);
    for (int i = 0; i < pc.src_offset.x; i++) {
        total += uvec4(round(subpassLoad(u_target, i) * pc.lod_k));
    }
    uint n = uint(pc.src_offset.x);
    o_copy = vec4((total + n / 2u) / n) / pc.lod_k;
#else
    o_copy = subpassLoad(u_target);
#endif
}
