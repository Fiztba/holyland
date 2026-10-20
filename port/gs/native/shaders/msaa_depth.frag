#version 450
// A multisampled Z target back into its image (--native-msaa): each pixel's first sample, as the
// colour's resolve has the pixel's average (depths are not averaged)
layout(set = 0, binding = 0) uniform sampler2DMS u_src;

void main() {
    gl_FragDepth = texelFetch(u_src, ivec2(gl_FragCoord.xy), 0).r;
}
