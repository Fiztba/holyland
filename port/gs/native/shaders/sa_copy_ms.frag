#version 450
// The scene's colour into a GS target's multisampled image, sample for sample (P8, gsn_standalone.inc,
// sa_release): both have the same samples; each invocation is one sample (gl_SampleID)
layout(set = 0, binding = 0) uniform sampler2DMS u_src;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = texelFetch(u_src, ivec2(gl_FragCoord.xy), gl_SampleID);
}
