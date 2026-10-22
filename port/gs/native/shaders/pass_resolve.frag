#version 450
// KF4_GSN_MSAA_BLEND=fast (port/gs/native/gsn.cpp, "Shader blending and the render pass"): a
// multisampled target's pixels resolved into every sample of the pass's second colour attachment
// (input attachment 1, the target, into colour attachment 1), each channel the mean of its samples
// rounded half up; colour attachment 0 is not written. (vkCmdResolveImage's mean is not always that:
// NVIDIA's rounds a half either way, the same two values in different places differently.)
layout(input_attachment_index = 1, set = 0, binding = 5) uniform subpassInputMS u_target;
layout(push_constant, std430) uniform Push {
    int samples;
    float scale; // 255, or 65535 for --native-colour high's 16-bit targets
} pc;
layout(location = 1) out vec4 o_copy;

void main() {
    uvec4 total = uvec4(0u);
    for (int i = 0; i < pc.samples; i++) {
        total += uvec4(round(subpassLoad(u_target, i) * pc.scale));
    }
    uint n = uint(pc.samples);
    o_copy = vec4((total + n / 2u) / n) / pc.scale;
}
