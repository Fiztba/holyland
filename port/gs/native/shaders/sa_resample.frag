#version 450
// The scene's targets and the GS's at scales and samples of their own (P8 T2, gsn_standalone.inc:
// sa_resample_pass; --scene-scale, --scene-msaa). Each pixel of the destination takes the source's pixels
// whose middles are inside it (all their samples), or the one under its middle where none are (a larger
// destination): the GS's picture taken into a finer scene is each of its pixels repeated, and the scene given
// back to a coarser GS the mean of each GS pixel's pixels and samples. A depth (SA_DEPTH) takes the one under
// its middle, as it is (zscale 0), or rounded as sa_depth.frag rounds it for the GS's Z buffer.
// SA_MS: the source is multisampled. The destination's samples, when it has them, all get the same.
layout(push_constant, std430) uniform Push {
    vec2 ratio;    // the source's pixels a destination pixel is, across and down
    ivec2 last;    // the source's last pixel
    int samples;   // the source's samples (SA_MS)
    float zscale;  // (SA_DEPTH) Z to depth, 1 / 2^24 or 1 / 2^32: rounded to the GS's whole Z; below 0, not
                   // rounded (--native-layer-z tolerant), within zmax; 0, as it is
    float zmax;    // the largest Z the format holds
} pc;
#ifdef SA_MS
layout(set = 0, binding = 0) uniform sampler2DMS u_src;
#else
layout(set = 0, binding = 0) uniform sampler2D u_src;
#endif
#ifndef SA_DEPTH
layout(location = 0) out vec4 o_color;
#endif

vec4 fetch(ivec2 p, int s) {
#ifdef SA_MS
    return texelFetch(u_src, p, s);
#else
    return texelFetch(u_src, p, 0);
#endif
}

void main() {
    vec2 d = floor(gl_FragCoord.xy);
    ivec2 mid = clamp(ivec2(floor((d + 0.5) * pc.ratio)), ivec2(0), pc.last);
#ifdef SA_DEPTH
    float z = fetch(mid, 0).r;
    gl_FragDepth = pc.zscale == 0.0 ? z
                   : pc.zscale < 0.0 ? min(z, -pc.zmax * pc.zscale)
                                     : clamp(roundEven(z / pc.zscale), 0.0, pc.zmax) * pc.zscale;
#else
    ivec2 lo = ivec2(ceil(d * pc.ratio - 0.5));
    ivec2 hi = ivec2(ceil((d + 1.0) * pc.ratio - 0.5)) - 1;
    if (hi.x < lo.x) {
        lo.x = hi.x = mid.x;
    }
    if (hi.y < lo.y) {
        lo.y = hi.y = mid.y;
    }
    lo = clamp(lo, ivec2(0), pc.last);
    hi = clamp(hi, ivec2(0), pc.last);
#ifdef SA_MS
    int samples = max(pc.samples, 1);
#else
    int samples = 1;
#endif
    vec4 sum = vec4(0.0);
    for (int y = lo.y; y <= hi.y; y++) {
        for (int x = lo.x; x <= hi.x; x++) {
            for (int s = 0; s < samples; s++) {
                sum += fetch(ivec2(x, y), s);
            }
        }
    }
    o_color = sum / float((hi.x - lo.x + 1) * (hi.y - lo.y + 1) * samples);
#endif
}
