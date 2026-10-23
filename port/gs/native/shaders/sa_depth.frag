#version 450
// The scene's depth into a GS Z buffer (P8, gsn_standalone.inc, sa_release). With --native-layer-z ps2 the GS
// keeps a whole Z per pixel, so the scene's Z (its fraction kept) is rounded as the GS's own draws round
// theirs (ties to even, within the format), for the GS's draws after it to test against what they would
// have found; with tolerant (zscale below 0) the GS keeps the fraction too, and it is copied as it is.
// SA_MS: from and to multisampled images, sample for sample.
layout(push_constant, std430) uniform Push {
    float zscale; // Z to depth: 1 / 2^24, or 1 / 2^32 for PSMZ32; below 0: not rounded
    float zmax;   // the largest Z the format holds
} pc;
#ifdef SA_MS
layout(set = 0, binding = 0) uniform sampler2DMS u_src;
#else
layout(set = 0, binding = 0) uniform sampler2D u_src;
#endif

void main() {
#ifdef SA_MS
    float d = texelFetch(u_src, ivec2(gl_FragCoord.xy), gl_SampleID).r;
#else
    float d = texelFetch(u_src, ivec2(gl_FragCoord.xy), 0).r;
#endif
    gl_FragDepth = pc.zscale < 0.0 ? min(d, -pc.zmax * pc.zscale) : clamp(roundEven(d / pc.zscale), 0.0, pc.zmax) * pc.zscale;
}
