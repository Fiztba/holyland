#version 450
// --native-colour high (port/gs/native/gsn.cpp, copy_lines): lines of a 16-bit target into an 8-bit image
// (the scanout's picture, the deinterlace's fields), drawn over the whole image with msaa.vert's
// triangle. The image's line `dst_first + dst_step * m` (sy rows of pixels each) is the target's line
// `first + step * m`, for m below count; the image's other lines are left as they are. Colours are
// dithered to 8 bits: a channel v (in 8-bit steps, with its fraction) becomes floor(v + t), t a
// threshold in [0, 1) from interleaved gradient noise (fine-grained, no pattern to see, the same every
// picture), so a gradient keeps its mean level everywhere instead of stepping. A whole number stays as
// it is. Alpha is rounded.
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(push_constant) uniform Push {
    ivec2 src;     // the target's pixel at the image's (0, 0) less the line offset: its x and its line 0's row
    int first;     // the target's first line
    int step;      // and the lines between
    int dst_first; // the image's first line
    int dst_step;
    int count;     // lines
    int sy;        // rows of pixels a line
} push;
layout(location = 0) out vec4 o_color;

float threshold(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int line = p.y / push.sy, sub = p.y - line * push.sy;
    int k = line - push.dst_first;
    if (k < 0 || k % push.dst_step != 0 || k / push.dst_step >= push.count) {
        discard;
    }
    int m = k / push.dst_step;
    vec4 v = texelFetch(u_src, ivec2(push.src.x + p.x, push.src.y + (push.first + push.step * m) * push.sy + sub), 0) * 255.0;
    // (a whole number read back as a hair under or over it stays that number)
    vec3 c = floor(v.rgb + 1.0 / 512.0 + threshold(vec2(p)) * (1.0 - 1.0 / 256.0));
    o_color = vec4(clamp(c, 0.0, 255.0), round(v.a)) / 255.0;
}
