#version 450
#extension GL_GOOGLE_include_directive : require
// The native GS's pixels (port/gs/native/gsn.cpp): the GS's texture lookup, texture function, fog,
// alpha test and (for the draws that need it) blending, in the GS's integer arithmetic.
//
// Textures come in two kinds. A texture decoded from VRAM is an integer image of the raw texels (an
// index for PSMT8 and PSMT4, looked up in the CLUT here; the 32, 24 or 16-bit colour); a texture that
// is a render target is the target itself (RGBA8, the GS's bytes). Both are read texel by texel, and
// filtered here as the GS filters (4 bits of fraction), so a CLUT change needs no new texture.
#include "gs_common.h"

layout(location = 0) noperspective in vec4 v_color;
layout(location = 1) noperspective in vec3 v_stq;
layout(location = 2) noperspective in float v_fog;

layout(set = 0, binding = 0) uniform usampler2D u_raw;
layout(set = 0, binding = 1) uniform sampler2D u_target;
layout(set = 0, binding = 2, std140) uniform Clut {
    uvec4 entries[64];
} u_clut;
layout(set = 0, binding = 3) uniform sampler2D u_dest;

layout(location = 0) out vec4 o_color;

uint field(uint v, uint shift, uint bits) {
    return (v >> shift) & ((1u << bits) - 1u);
}

ivec4 unpack32(uint v) {
    return ivec4(v & 0xFFu, (v >> 8) & 0xFFu, (v >> 16) & 0xFFu, v >> 24);
}

// TEXA: the alpha of a texel with none of its own (24-bit), or one bit of it (16-bit)
int texa_alpha(ivec3 rgb, bool bit, bool has_bit) {
    if (has_bit && bit) {
        return int(field(pc.texa, 16u, 8u));
    }
    if ((pc.texa & 0x8000u) != 0u && rgb == ivec3(0)) {
        return 0;
    }
    return int(field(pc.texa, 0u, 8u));
}

// A texture that is a render target drawn at a larger scale (--native-scale) is read at its own
// pixels: the coordinates are in them, `ts` to a texel, and wrap as the texel they are in does
int target_scale() {
    return field(pc.flags, GSN_F_SRC_SHIFT, 4u) >= GSN_SRC_TARGET_CT32 ? max(int(pc.tscale), 1) : 1;
}

// One texel at integer coordinates, wrapped as CLAMP says, at mip level `level`
ivec4 texel(ivec2 c, int level) {
    int ts = target_scale();
    ivec2 logical = ivec2(floor(vec2(c) / float(ts)));
    ivec2 sub = c - logical * ts;
    c = logical;
    ivec2 size = max(ivec2(int(pc.tex_size & 0xFFFFu), int(pc.tex_size >> 16)) >> level, ivec2(1));
    uint wms = field(pc.clamp_lo, 0u, 2u);
    uint wmt = field(pc.clamp_lo, 2u, 2u);
    ivec2 lo = ivec2(int(field(pc.clamp_lo, 4u, 10u)), int(field(pc.clamp_hi, 0u, 10u)));
    ivec2 hi = ivec2(int(field(pc.clamp_lo, 14u, 10u)), int(field(pc.clamp_hi, 10u, 10u)));
    uvec2 mode = uvec2(wms, wmt);

    for (int i = 0; i < 2; i++) {
        if (mode[i] == 0u) {
            c[i] &= size[i] - 1;
        } else if (mode[i] == 1u) {
            c[i] = clamp(c[i], 0, size[i] - 1);
        } else if (mode[i] == 2u) {
            c[i] = clamp(c[i], lo[i] >> level, hi[i] >> level);
        } else {
            c[i] = (c[i] & lo[i]) | hi[i];
        }
    }

    uint src = field(pc.flags, GSN_F_SRC_SHIFT, 4u);
    if (src >= GSN_SRC_TARGET_CT32) {
        ivec4 t = ivec4(round(texelFetch(u_target, (c + pc.src_offset) * ts + sub, 0) * 255.0));
        if (src == GSN_SRC_TARGET_CT24) {
            t.a = texa_alpha(t.rgb, false, false);
        } else if (src == GSN_SRC_TARGET_CT16) {
            t.rgb &= 0xF8;
            t.a = texa_alpha(t.rgb, t.a >= 0x80, true);
        }
        return t;
    }
    uint raw = texelFetch(u_raw, c, level).r;
    if (src == GSN_SRC_CLUT) {
        return unpack32(u_clut.entries[raw >> 2][raw & 3u]);
    } else if (src == GSN_SRC_CT32) {
        return unpack32(raw);
    } else if (src == GSN_SRC_CT24) {
        ivec4 t = unpack32(raw);
        t.a = texa_alpha(t.rgb, false, false);
        return t;
    }
    ivec4 t = ivec4((raw & 0x1Fu) << 3, ((raw >> 5) & 0x1Fu) << 3, ((raw >> 10) & 0x1Fu) << 3, 0);
    t.a = texa_alpha(t.rgb, (raw & 0x8000u) != 0u, true);
    return t;
}

// v / 2^bits, rounded to nearest, ties to even: the mipmap levels' weighted sum
ivec4 round_shift(ivec4 v, int bits) {
    ivec4 q = v >> bits;
    ivec4 r = v & ((1 << bits) - 1);
    int half_ = 1 << (bits - 1);
    return q + ivec4(greaterThan(r, ivec4(half_))) + ivec4(equal(r, ivec4(half_))) * (q & 1);
}

// The texture at texel coordinates `tc` (level 0's), one level, nearest or bilinear
ivec4 sample_level(vec2 tc, int level, bool linear) {
    vec2 at = tc / float(1 << level) * float(target_scale());
    // the GS's texel coordinates have 4 bits of fraction; interpolated in floats, a coordinate that is
    // exactly on one comes out a hair under it, which the bias puts back
    ivec2 p = ivec2(floor(at * 16.0 + 1.0 / 64.0));
    if (!linear) {
        return texel(p >> 4, level);
    }
    p -= 8;
    ivec2 i = p >> 4;
    ivec2 f = p & 15;
    ivec4 c00 = texel(i, level);
    ivec4 c10 = texel(i + ivec2(1, 0), level);
    ivec4 c01 = texel(i + ivec2(0, 1), level);
    ivec4 c11 = texel(i + ivec2(1, 1), level);
    // rounded to nearest, a half up (as paraLLEl-GS has it: a texel a pixel at the GS's half-texel
    // offset makes every pixel such a half, and ties to even left half of them 1 lower)
    return (c00 * (16 - f.x) * (16 - f.y) + c10 * f.x * (16 - f.y) + c01 * (16 - f.x) * f.y + c11 * f.x * f.y + 128) >> 8;
}

ivec4 sample_texture() {
    vec2 size = vec2(float(pc.tex_size & 0xFFFFu), float(pc.tex_size >> 16));
    vec2 tc = (pc.flags & GSN_F_FST) != 0u ? v_stq.xy : v_stq.xy / v_stq.z * size;
    uint mmag = field(pc.tex1, 0u, 1u);
    uint mmin = field(pc.tex1, 1u, 3u);
    int mxl = int(field(pc.tex1, 4u, 3u));
    float lod = pc.lod_k;
    if (field(pc.tex1, 7u, 1u) == 0u) {
        lod += log2(1.0 / abs(v_stq.z)) * float(1u << field(pc.tex1, 8u, 2u));
    }
    if (lod <= 0.0 || mxl == 0 && mmin < 2u) {
        return sample_level(tc, 0, lod <= 0.0 ? mmag == 1u : mmin == 1u);
    }
    if (mmin < 2u) {
        return sample_level(tc, 0, mmin == 1u);
    }
    bool linear = mmin == 4u || mmin == 5u;
    lod = min(lod, float(mxl));
    if (mmin == 2u || mmin == 4u) {
        return sample_level(tc, min(int(floor(lod + 0.5)), mxl), linear);
    }
    int l0 = int(floor(lod));
    int l1 = min(l0 + 1, mxl);
    int w = int(floor(fract(lod) * 16.0));
    return round_shift(sample_level(tc, l0, linear) * (16 - w) + sample_level(tc, l1, linear) * w, 4);
}

bool alpha_test(int a) {
    int ref = int(field(pc.test, 4u, 8u));
    switch (field(pc.test, 1u, 3u)) {
    case 0u: return false;
    case 1u: return true;
    case 2u: return a < ref;
    case 3u: return a <= ref;
    case 4u: return a == ref;
    case 5u: return a >= ref;
    case 6u: return a > ref;
    default: return a != ref;
    }
}

void main() {
    // GS colours are integers; a colour that is the same at every corner comes through exactly
    ivec4 cf = ivec4(floor(v_color + 1.0 / 256.0));
    ivec4 c = cf;

    if ((pc.flags & GSN_F_TME) != 0u) {
        ivec4 ct = sample_texture();
        uint tfx = field(pc.flags, GSN_F_TFX_SHIFT, 2u);
        bool tcc = (pc.flags & GSN_F_TCC) != 0u;
        if (tfx == 0u) { // MODULATE
            c.rgb = min((cf.rgb * ct.rgb) >> 7, ivec3(255));
            c.a = tcc ? min((cf.a * ct.a) >> 7, 255) : cf.a;
        } else if (tfx == 1u) { // DECAL
            c.rgb = ct.rgb;
            c.a = tcc ? ct.a : cf.a;
        } else if (tfx == 2u) { // HIGHLIGHT
            c.rgb = min(((cf.rgb * ct.rgb) >> 7) + cf.a, ivec3(255));
            c.a = tcc ? min(ct.a + cf.a, 255) : cf.a;
        } else { // HIGHLIGHT2
            c.rgb = min(((cf.rgb * ct.rgb) >> 7) + cf.a, ivec3(255));
            c.a = tcc ? ct.a : cf.a;
        }
    }
    if ((pc.flags & GSN_F_FGE) != 0u) {
        // F rounded, and the blend towards FOGCOL rounded too (as paraLLEl-GS has it, from tests on the
        // hardware: F 255, FOGCOL 128 and a colour of 128 give 128, not 127)
        int f = clamp(int(roundEven(v_fog)), 0, 255);
        ivec3 fog = ivec3(unpack32(pc.fogcol).rgb);
        c.rgb = ivec3(floor(vec3(fog) + vec3((c.rgb - fog) * f) / 256.0 + 0.5));
    }

    uint scanmsk = field(pc.misc, GSN_M_SCANMSK_SHIFT, 2u);
    uint line = uint(gl_FragCoord.y) / max(field(pc.misc, GSN_M_SCALE_SHIFT, 3u), 1u);
    if (scanmsk >= 2u && (line & 1u) == scanmsk - 2u) {
        discard;
    }

    uint pass = field(pc.flags, GSN_F_PASS_SHIFT, 2u);
    if (pass != 0u && alpha_test(c.a) != (pass == 1u)) {
        discard;
    }

    if ((pc.flags & GSN_F_SHADER_BLEND) != 0u) {
        ivec4 d = ivec4(round(texelFetch(u_dest, ivec2(gl_FragCoord.xy) - pc.dst_offset, 0) * 255.0));
        if ((pc.misc & GSN_M_DST16) != 0u) {
            d.rgb &= 0xF8;
            d.a = d.a >= 0x80 ? 0x80 : 0;
        }
        if ((pc.flags & GSN_F_DATE) != 0u && ((d.a >> 7) & 1) != int(field(pc.test, 15u, 1u))) {
            discard;
        }
        ivec4 o = c;
        if ((pc.misc & GSN_M_PABE) == 0u || c.a >= 0x80) {
            uint sa = field(pc.alpha, 0u, 2u), sb = field(pc.alpha, 2u, 2u);
            uint sc = field(pc.alpha, 4u, 2u), sd = field(pc.alpha, 6u, 2u);
            ivec3 a = sa == 0u ? c.rgb : sa == 1u ? d.rgb : ivec3(0);
            ivec3 b = sb == 0u ? c.rgb : sb == 1u ? d.rgb : ivec3(0);
            int k = sc == 0u ? c.a : sc == 1u ? d.a : int(field(pc.alpha, 8u, 8u));
            ivec3 dd = sd == 0u ? c.rgb : sd == 1u ? d.rgb : ivec3(0);
            o.rgb = (((a - b) * k) >> 7) + dd;
        }
        o.rgb = (pc.misc & GSN_M_COLCLAMP) != 0u ? clamp(o.rgb, 0, 255) : o.rgb & 0xFF;
        if ((pc.misc & GSN_M_FBA) != 0u) {
            o.a |= 0x80;
        }
        if ((pc.misc & GSN_M_DST16) != 0u) {
            o.rgb &= 0xF8;
            o.a &= 0x80;
        }
        if ((pc.misc & GSN_M_DST24) != 0u) {
            o.a = d.a;
        }
        uint m = pc.fbmsk;
        uint ou = uint(o.r) | (uint(o.g) << 8) | (uint(o.b) << 16) | (uint(o.a & 0xFF) << 24);
        uint du = uint(d.r) | (uint(d.g) << 8) | (uint(d.b) << 16) | (uint(d.a) << 24);
        o_color = vec4(unpack32((ou & ~m) | (du & m))) / 255.0;
        return;
    }

    int a = c.a;
    if ((pc.misc & GSN_M_FBA) != 0u) {
        a |= 0x80;
    }
    if ((pc.misc & GSN_M_DST16) != 0u) {
        c.rgb &= 0xF8;
        a &= 0x80;
    }
    if ((pc.flags & GSN_F_PREMUL) != 0u) {
        int k = field(pc.alpha, 4u, 2u) == 0u ? c.a : int(field(pc.alpha, 8u, 8u));
        c.rgb = min((c.rgb * k + ((pc.flags & GSN_F_PREMUL_CEIL) != 0u ? 127 : 0)) >> 7, ivec3(255));
    }
    o_color.rgb = vec3(c.rgb) / 255.0;
    o_color.a = (pc.flags & GSN_F_ALPHA_FACTOR) != 0u ? float(c.a) / 128.0 : float(a) / 255.0;
}
