#version 450
// The picture of a frame the scene drew at a scale of its own (P8 T2, gsn_composite.inc; --scene-scale): the
// scene's picture with what the GS drew over it after it was given back (sa_release), at the scene's
// resolution.
//
// The GS has the scene's picture at its own scale (lo: as sa_release gave it) and drew the rest over it (final):
// the 2D, the fades, the brightness. What it drew over the whole screen, one colour over every pixel (a fill, a
// fade, the brightness, the tint: the chain), is done to the scene's picture here too; a pixel whose final colour
// is what the chain makes of lo (within tol) is the scene's own, and is drawn from the scene's picture (hi), its
// every pixel through the chain; any other has the GS's 2D over it, and is the GS's (each of its pixels repeated).
layout(set = 0, binding = 0) uniform sampler2D u_final; // the GS's frame, as the scanout reads it
layout(set = 0, binding = 1) uniform sampler2D u_lo;    // the GS's frame as the scene gave it
layout(set = 0, binding = 2) uniform sampler2D u_hi;    // the scene's picture (its samples resolved)
// the chain, in order: x the kind (bits 0-7: 1 scale, 2 blend, 3 set) and COLCLAMP (bit 8); y the colour (RGBA);
// z ALPHA's A, B, C, D (2 bits each); w its FIX
layout(set = 0, binding = 3, std140) uniform Chain {
    uvec4 ops[8];
} u_chain;
layout(push_constant, std430) uniform Push {
    vec2 to_gs;      // the GS image's pixels a picture pixel is, across and down
    ivec2 gs_origin; // the GS image's pixel of the picture's (0, 0)
    ivec2 hi_origin; // the scene picture's pixel of the picture's (0, 0)
    int count;       // the chain's ops
    float tol;       // how far (in levels) a pixel the GS left alone can be from the chain's colour
} pc;
layout(location = 0) out vec4 o_color;

int pick(uint sel, int cs, int cd) {
    return sel == 0u ? cs : sel == 1u ? cd : 0;
}

ivec3 chain(ivec3 c) {
    for (int i = 0; i < pc.count; i++) {
        uvec4 op = u_chain.ops[i];
        uint kind = op.x & 0xFFu;
        ivec4 s = ivec4(op.y & 0xFFu, (op.y >> 8) & 0xFFu, (op.y >> 16) & 0xFFu, op.y >> 24);
        if (kind == 1u) {
            // the frame as a texture times the colour (MODULATE), no blending
            c = min((c * s.rgb) >> 7, ivec3(255));
        } else if (kind == 2u) {
            // (A - B) * C >> 7 + D, the GS's blend of a flat colour over the frame (C: As or FIX)
            uint a = op.z & 3u, b = (op.z >> 2) & 3u, cc = (op.z >> 4) & 3u, d = (op.z >> 6) & 3u;
            int k = cc == 0u ? s.a : int(op.w & 0xFFu);
            ivec3 v;
            for (int j = 0; j < 3; j++) {
                v[j] = ((pick(a, s[j], c[j]) - pick(b, s[j], c[j])) * k >> 7) + pick(d, s[j], c[j]);
            }
            c = (op.x & 0x100u) != 0u ? clamp(v, ivec3(0), ivec3(255)) : (v & ivec3(0xFF));
        } else if (kind == 3u) {
            c = s.rgb;
        }
    }
    return c;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 g = ivec2(floor((vec2(p) + 0.5) * pc.to_gs)) + pc.gs_origin;
    vec3 f = texelFetch(u_final, g, 0).rgb * 255.0;
    ivec3 l = ivec3(round(texelFetch(u_lo, g, 0).rgb * 255.0));
    vec3 want = vec3(chain(l));
    if (all(lessThanEqual(abs(f - want), vec3(pc.tol)))) {
        ivec3 h = ivec3(round(texelFetch(u_hi, p + pc.hi_origin, 0).rgb * 255.0));
        o_color = vec4(vec3(chain(h)) / 255.0, 1.0);
    } else {
        o_color = vec4(f / 255.0, 1.0);
    }
}
