#version 450
// Shader blending in the render pass (port/gs/native/gsn.cpp): a single-sampled target's pixels copied
// into the pass's second colour attachment (input attachment 0, the target, into colour attachment 1),
// where the next run of a blending draw covers them; colour attachment 0 is not written
layout(input_attachment_index = 0, set = 0, binding = 4) uniform subpassInput u_target;
layout(location = 1) out vec4 o_copy;

void main() {
    o_copy = subpassLoad(u_target);
}
