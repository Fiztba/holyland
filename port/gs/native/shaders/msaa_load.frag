#version 450
// A colour target's image into every sample of its multisampled one (--native-msaa): the pixel's
// colour, written to all of its samples
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = texelFetch(u_src, ivec2(gl_FragCoord.xy), 0);
}
