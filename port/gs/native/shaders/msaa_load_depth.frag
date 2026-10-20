#version 450
// A Z target's image into every sample of its multisampled one (--native-msaa): the pixel's depth,
// written to all of its samples
layout(set = 0, binding = 0) uniform sampler2D u_src;

void main() {
    gl_FragDepth = texelFetch(u_src, ivec2(gl_FragCoord.xy), 0).r;
}
