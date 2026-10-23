// ---- Native draw: the screen passes (P7-1) ----------------------------------------------------------
// Included at the end of gsn_native_draw.inc (inside gsn.cpp's namespace). A draw of program RD_PROGRAM_PASS
// carries a pass of GameLoop_DrawFrame's as data (render.h, RdPass; game/native_draw/nd_passes.c): the
// brightness and the tint, the fills and the fade, the flicker filter, the frame shown before blended in,
// the view-turn shift, haze and the wobble grid. The PS2 path draws each as register lists and GIF packets
// of sprites, passed on by VU1's program 0x276; here they are drawn into the GS's targets as those packets
// would have been: the same register writes in the same order (so the GS's state, its batches and what is
// drawn after are the PS2 path's), and the same primitives through the GS's own rasteriser and fragment
// shader (gs.frag: the GS's arithmetic, so --native-colour exact is exact), but none of VU1's runs.
//
// Where it is the same picture, a pass is one sprite instead of the game's 64-pixel strips: the brightness
// and the tint (the frame read one texel a pixel, nearest, each pixel its own: ten strips side by side are
// one sprite) and the flat fills (one colour). The registers they leave are the last strip's, which are the
// sprite's. The flicker filter, the view-turn shift and the wobble grid keep the game's primitives: the
// filter's strips are 63.5 pixels wide over 64 texels (a column of each is never drawn), the shift's are
// SCANMSK's fields, and the grid's cells are its shape.
//
// Pictures between (--frame-rate). The PS2 path's passes are 2D: frame_interp.c pairs them with the frame
// before's (flat_pair) and draws them t of the way from there: a fill's colour, the wobble grid's texels,
// and the frame shown before blended in by A^t (reading the native GS's copy of it, take_snapshot). A pass
// is paired here by its identity instead (the pass, its call site and its count in the frame: rd_pass) with
// the same pass of the frame before (nd.pass_before), and its corners are drawn by shaders/nd_pass.vert,
// which does those moves on the GPU from a record of the pass's (NdPassBetween), so that a recording drawn
// again for another picture moves them again. Its vertex kicks are not the frame's (frame_interp.c never
// saw them: a kick counted would put every later one out by one), so they are queued uncounted
// (nd.pass_kicking) and the pass is a batch of its own whose corners are the CPU's (nd.pass_pending).

void packed(uint32_t desc, const uint64_t q[2]);

// What nd_pass.vert reads of a pass in a picture between (set 2, binding 0)
enum : uint32_t {
    ND_PASS_RGBA = 0x1, // the corners' colour t of the way from the frame before's (prev_rgba)
    ND_PASS_PREV = 0x2, // the frame shown before, blended in by A^t (A t of the way from prev_rgba's when paired)
    ND_PASS_PAIRED = 0x4,
    ND_PASS_GRID = 0x8, // the wobble grid's texels t of the way from the frame before's (prev_uv, by the corner's place)
};
struct NdPassBetween {
    uint32_t mode, prev_rgba, pad[2];
    uint32_t prev_uv[88], uv[88]; // (U | V << 16, 12.4: the frame before's and this frame's)
};

// A GIF packet built here as the game builds its packet, then walked as gif() walks a PACKED one (each tag:
// PRE's PRIM, Q set to 1; then each register). (Not gif() itself: the PORT_GS_REG_NATIVE_DRAW write this
// comes from is in the middle of a path's packet.)
struct NdPassPacket {
    std::vector<uint64_t> q;
    void tag(uint64_t lo, uint64_t regs) {
        q.push_back(lo);
        q.push_back(regs);
    }
    void words(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        q.push_back(uint64_t(a) | uint64_t(b) << 32);
        q.push_back(uint64_t(c) | uint64_t(d) << 32);
    }
    void ad(uint32_t reg, uint64_t value) {
        q.push_back(value);
        q.push_back(reg);
    }
};

// (a picture between, or frame_interp.c's replay: the frame drawn read where the picture is drawn, as
// frame_interp.c's ad_write writes the PS2 path's TEX0: from, to; else none)
uint32_t nd_pass_tex_from = 0xFFFFFFFFu, nd_pass_tex_to = 0;

void nd_pass_walk(const NdPassPacket &p) {
    size_t n = p.q.size() / 2;
    for (size_t i = 0; i < n;) {
        uint64_t tag = p.q[i * 2], regs = p.q[i * 2 + 1];
        i++;
        uint32_t nloop = uint32_t(tag & 0x7FFF), nreg = uint32_t(tag >> 60);
        nreg = nreg ? nreg : 16;
        if ((tag >> 46) & 1) {
            gs_write(R_PRIM, (tag >> 47) & 0x7FF);
        }
        gs->internal_q = 0x3F800000u; // (a tag sets Q to 1)
        interp.q_src = GSN_IM_NONE;
        for (uint32_t k = 0; k < nloop * nreg && i < n; k++, i++) {
            uint32_t desc = uint32_t(regs >> ((k % nreg) * 4)) & 0xF;
            if (interp.active && (desc == 0x4 || desc == 0x5 || desc == 0xC || desc == 0xD)) {
                nd.pass_pending = true; // (a picture between's: the batch is the pass's, its corners the CPU's)
            }
            uint64_t q[2] = {p.q[i * 2], p.q[i * 2 + 1]};
            if (desc == 0xE && ((q[1] & 0xFF) == R_TEX0_1 || (q[1] & 0xFF) == R_TEX0_2) && (q[0] & 0x3FFF) == nd_pass_tex_from) {
                q[0] = (q[0] & ~0x3FFFull) | nd_pass_tex_to;
            }
            packed(desc, q);
        }
    }
}

// A register list (Packet_BeginGsRegisters ... _End): an A+D packet of its own
void nd_pass_regs(std::initializer_list<std::pair<uint32_t, uint64_t>> regs) {
    NdPassPacket p;
    p.tag(uint64_t(regs.size()) | (1ull << 15) | (1ull << 60), 0xE);
    for (const auto &r : regs) {
        p.ad(r.first, r.second);
    }
    nd_pass_walk(p);
}

// A corner of a sprite, as ScreenSprite's (gfx/packet.h): X and Y (12.4), Z, and its texel (12.4)
struct NdPassCorner {
    uint32_t x, y, z;
    int32_t s, t;
};

// GsDraw_QueueTexturedSprite's packet: PRIM (with the sprite type) by PRE, TEX0 and TEX1 by A+D, then each
// corner's UV, RGBAQ and XYZ2 (the first corner's XYZ2 with a 1 in its last word, as the game writes it)
void nd_pass_textured_sprite(uint32_t prim, uint64_t tex0, uint64_t tex1, const uint32_t rgba[4], const NdPassCorner &a,
                             const NdPassCorner &b) {
    NdPassPacket p;
    p.tag((uint64_t(prim | 6) << 47) | 0x8000400000008001ull, 0x513513EE);
    p.ad(R_TEX0_1, tex0);
    p.ad(R_TEX1_1, tex1);
    p.words(uint32_t(a.s), uint32_t(a.t), 0, 0);
    p.words(rgba[0], rgba[1], rgba[2], rgba[3]);
    p.words(a.x, a.y, a.z, 1);
    p.words(uint32_t(b.s), uint32_t(b.t), 0, 0);
    p.words(rgba[0], rgba[1], rgba[2], rgba[3]);
    p.words(b.x, b.y, b.z, 0);
    nd_pass_walk(p);
}

// GsDraw_QueueFlatSprite's packet: PRIM by PRE (texture, fog and ST off, the sprite type), then each corner's
// RGBAQ and XYZ2
void nd_pass_flat_sprite(uint32_t prim, const uint32_t rgba[4], const NdPassCorner &a, const NdPassCorner &b) {
    NdPassPacket p;
    p.tag((uint64_t((prim & 0xFFFFFFE9u) | 6) << 47) | 0x4000400000008001ull, 0x5151);
    p.words(rgba[0], rgba[1], rgba[2], rgba[3]);
    p.words(a.x, a.y, a.z, 1);
    p.words(rgba[0], rgba[1], rgba[2], rgba[3]);
    p.words(b.x, b.y, b.z, 0);
    nd_pass_walk(p);
}

// The 13-qword sprite packet Game_BlendPreviousFrame and Game_DrawHazeLayers build themselves (tag
// 0x80AF400000008001: a textured, blended sprite by PRE; TEX0 and TEX1 by A+D, then UV, RGBAQ and XYZ2
// twice, each XYZ2's last word 0)
void nd_pass_own_sprite(uint64_t tex0, uint64_t tex1, const uint32_t rgba[4], const NdPassCorner &a, const NdPassCorner &b) {
    NdPassPacket p;
    p.tag(0x80AF400000008001ull, 0x513513EE);
    p.ad(R_TEX0_1, tex0);
    p.ad(R_TEX1_1, tex1);
    p.words(uint32_t(a.s), uint32_t(a.t), 0, 0);
    p.words(rgba[0], rgba[1], rgba[2], rgba[3]);
    p.words(a.x, a.y, a.z, 0);
    p.words(uint32_t(b.s), uint32_t(b.t), 0, 0);
    p.words(rgba[0], rgba[1], rgba[2], rgba[3]);
    p.words(b.x, b.y, b.z, 0);
    nd_pass_walk(p);
}

// The frame (640 x 448 of a 1024 x 512 CT32 texture) as the passes read it
uint64_t nd_pass_frame_tex0(uint32_t tbp) {
    return uint64_t(tbp | 0x28028000u) | (9ull << 30);
}

// Game_ApplyBrightness and Game_TintScreen: the frame times RGB / 128, as one sprite (the game's ten strips
// each read their own pixels, one texel a pixel, nearest)
void nd_pass_scale(const RdPass &p) {
    nd_pass_regs({{R_TEXFLUSH, 0xFFFFFFFFull}}); // (GsDraw_QueueTexFlush; GsDraw_QueueFlush is the VIF's)
    nd_pass_regs({{R_ALPHA_1, 10}, {R_TEST_1, 0x30000}, {R_ZBUF_1, 0x10A000118ull}});
    uint32_t rgba[4] = {p.arg[0], p.arg[1], p.arg[2], 0x80};
    nd_pass_textured_sprite(0x116, nd_pass_frame_tex0(p.frame_tbp), 0, rgba, {0x6C00, 0x7200, 0xFFFFFFFFu, 0, 0},
                            {0x9400, 0x8DF8, 0xFFFFFFFFu, 0x2800, 0x1C00});
    nd_pass_regs({{R_ZBUF_1, 0xA000118}});
    nd_pass_regs({{R_TEXFLUSH, 0xFFFFFFFFull}});
}

// GsDraw_FillScreen: a flat colour over the screen, blended by ALPHA (one sprite for the game's ten strips)
void nd_pass_fill(uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint64_t blend) {
    nd_pass_regs({{R_ALPHA_1, blend}, {R_TEST_1, 0x31001}});
    uint32_t rgba[4] = {r, g, b, a};
    nd_pass_flat_sprite(0x46, rgba, {0x6C00, 0x7200, 0, 0, 0}, {0x9400, 0x8E00, 0, 0, 0});
}

// Game_DrawFade's drawing: Z off around a fill
void nd_pass_fade(const RdPass &p) {
    nd_pass_regs({{R_TEST_1, 0x30000}, {R_ZBUF_1, 0x10A000118ull}});
    nd_pass_fill(p.arg[0], p.arg[1], p.arg[2], p.arg[3], 0x44);
    nd_pass_regs({{R_ZBUF_1, 0xA000118}});
}

// Game_BlendScreenOffsetDown (and _UpDown): the frame over itself a row lower (and a row higher) at half,
// in the game's strips (each 63.5 pixels wide over 64 texels); its strips only with --flicker-filter
void nd_pass_flicker(const RdPass &p) {
    nd_pass_regs({{R_TEXFLUSH, 0xFFFFFFFFull}, {R_ALPHA_1, 0x44}, {R_TEST_1, 0x30000}, {R_ZBUF_1, 0x10A000118ull}});
    uint64_t tex0 = nd_pass_frame_tex0(p.frame_tbp);
    uint32_t rgba[4] = {0x80, 0x80, 0x80, 0x40};
    if (p.arg[1]) {
        NdPassCorner a = {0x6FF8, 0x8E08, 0xFFFFFFFFu, 0x400, 0x1C00}, b = {0x6C00, 0x7210, 0xFFFFFFFFu, 0, 0};
        for (int i = 0; i < 10; i++) {
            nd_pass_textured_sprite(0x156, tex0, 0, rgba, a, b);
            a.x += 0x400, b.x += 0x400, a.s += 0x400, b.s += 0x400;
        }
        if (p.arg[0]) {
            a = {0x6BF8, 0x71E8, 0xFFFFFFFFu, 0, 0};
            b = {0x7000, 0x8DF0, 0xFFFFFFFFu, 0x400, 0x1C00};
            for (int i = 0; i < 10; i++) {
                nd_pass_textured_sprite(0x156, tex0, 0, rgba, a, b);
                a.x += 0x400, b.x += 0x400, a.s += 0x400, b.s += 0x400;
            }
        }
    }
    nd_pass_regs({{R_ZBUF_1, 0xA000118}});
}

// Game_BlendPreviousFrame: the frame shown before (the other buffer) over the whole screen at the alpha
void nd_pass_previous(const RdPass &p) {
    nd_pass_regs({{R_ALPHA_1, 0x44}, {R_TEST_1, 0x51001}});
    const int32_t *r = reinterpret_cast<const int32_t *>(&p.arg[1]); // (the screen's, then the texels' rectangle)
    uint32_t rgba[4] = {0x80, 0x80, 0x80, p.arg[0]};
    nd_pass_own_sprite(nd_pass_frame_tex0(p.frame_tbp), 0, rgba,
                       {uint32_t((r[0] + 0x6C0) << 4), uint32_t((r[1] + 0x720) << 4), 0xFFFFFFFFu, r[4] << 4, r[5] << 4},
                       {uint32_t((r[2] << 4) + 0x6BF8), uint32_t((r[3] << 4) + 0x71F8), 0xFFFFFFFFu, r[6] << 4, r[7] << 4});
}

// Game_ShiftScreenByViewTurn's twenty strips (its registers are the game's, before): the first strip's
// sprite as the game made it (GsSpritePacket), each next one a step along
void nd_pass_view_shift(const RdPass &p) {
    const uint32_t *w = p.data;
    uint64_t tex0 = w[0] | uint64_t(w[1]) << 32, tex1 = w[2] | uint64_t(w[3]) << 32;
    uint32_t prim = w[4];
    uint32_t rgba[4] = {w[5] & 0xFF, (w[5] >> 8) & 0xFF, (w[5] >> 16) & 0xFF, w[5] >> 24};
    NdPassCorner c[2];
    for (int i = 0; i < 2; i++) {
        const uint32_t *k = w + 8 + i * 4; // (GsSpriteCorner: X and Y in halves, Z, the texel's X and Y)
        c[i] = {k[0] & 0xFFFF, k[0] >> 16, k[1], int32_t(k[2]), int32_t(k[3])};
    }
    int32_t step = int32_t(p.arg[0]);
    for (int i = 0; i < 20; i++) {
        nd_pass_textured_sprite(prim, tex0, tex1, rgba, c[0], c[1]);
        for (auto &k : c) {
            k.x = (k.x + uint32_t(step)) & 0xFFFF; // (a u16 in the game's struct)
            k.s += step;
        }
    }
}

// Game_DrawHazeLayers: a sprite of the frame at each layer's depth, bilinear and half a pixel over, redrawing
// the pixels farther than it
void nd_pass_haze(const RdPass &p) {
    nd_pass_regs({{R_ALPHA_1, 0x80000000A8ull}, {R_TEST_1, 0x51001}, {R_CLAMP_1, 5}});
    uint32_t rgba[4] = {p.arg[1], p.arg[2], p.arg[3], p.arg[4]};
    for (uint32_t i = 0; i < p.arg[0] && i < 8; i++) {
        nd_pass_own_sprite(nd_pass_frame_tex0(p.frame_tbp), 0x60, rgba, {0x6BF8, 0x7200, p.data[i], 0, 0},
                           {0x93F8, 0x8E00, p.data[i], 0x2800, 0x1C00});
    }
    nd_pass_regs({{R_CLAMP_1, 0}});
}

// Game_DrawWobblingScreen's 70 cells (its registers are the game's, before and after): each a strip of four
// corners, the grid's points on the screen with their texels in the frame grabbed at 0x2BC0
void nd_pass_wobble(const RdPass &p) {
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 10; j++) {
            NdPassPacket q;
            q.tag(0xC0AE400000008001ull, 0x513513513513ull);
            for (int k = 0; k < 2; k++) {
                for (int c = 0; c < 2; c++) {
                    uint32_t uv = p.data[(i + k) * 11 + j + c];
                    q.words(uv & 0x3FFF, uv >> 16, 0, 0);
                    q.words(p.arg[0], p.arg[1], p.arg[2], p.arg[3]);
                    q.words(uint32_t((((j + c) << 6) + 0x6C0) << 4), uint32_t((((i + k) << 6) + 0x720) << 4), 0, 0);
                }
            }
            nd_pass_walk(q);
        }
    }
}

// The pass's program in a picture between: the GS's fragment shader with nd_pass.vert
Program *nd_pass_program(Program *p) {
    auto it = nd.pass_programs.find(p);
    if (it != nd.pass_programs.end()) {
        return it->second;
    }
    if (!nd.pass_vs) {
        ResourceLayout vert = {};
        vert.input_mask = 0x1F;
        vert.push_constant_size = sizeof(Push);
        vert.sets[1].storage_buffer_mask = 1u;
        vert.sets[1].meta[0].array_size = 1;
        vert.sets[2].storage_buffer_mask = 3u;
        vert.sets[2].meta[0].array_size = 1;
        vert.sets[2].meta[1].array_size = 1;
        nd.pass_vs = gs->device->request_shader(gsn_spv_nd_pass_vert, sizeof(gsn_spv_nd_pass_vert), &vert);
    }
    Program *np = gs->device->request_program(nd.pass_vs, p->get_shader(ShaderStage::Fragment));
    nd.pass_programs[p] = np;
    return np;
}

// A picture between's record of the pass: how its corners move, from the frame before's same pass
void nd_pass_between(const NdKey &key, const RdPass &p) {
    NdPassBetween b = {};
    auto it = nd.pass_before.find(key);
    const RdPass *was = it != nd.pass_before.end() ? &it->second : nullptr;
    auto rgba_of = [](const uint32_t *a) {
        return (a[0] & 0xFF) | (a[1] & 0xFF) << 8 | (a[2] & 0xFF) << 16 | (a[3] & 0xFF) << 24;
    };
    switch (p.pass) {
    case RD_PASS_FILL:
    case RD_PASS_FADE:
    case RD_PASS_WOBBLE:
        if (was && rgba_of(was->arg) != rgba_of(p.arg)) {
            b.mode |= ND_PASS_RGBA;
            b.prev_rgba = rgba_of(was->arg);
        }
        if (p.pass == RD_PASS_WOBBLE && was && memcmp(was->data, p.data, 88 * 4) != 0) {
            b.mode |= ND_PASS_GRID;
            memcpy(b.prev_uv, was->data, sizeof(b.prev_uv));
            memcpy(b.uv, p.data, sizeof(b.uv));
        }
        break;
    case RD_PASS_SCALE:
        if (was && memcmp(was->arg, p.arg, 12) != 0) {
            b.mode |= ND_PASS_RGBA;
            b.prev_rgba = rgba_of(was->arg) | 0x80000000u;
        }
        break;
    case RD_PASS_PREVIOUS:
        // (frame_interp.c: when the frame shown before was kept for the pictures; paired when its alpha changed)
        if (snapshot.image && snapshot.bp == p.frame_tbp) {
            b.mode |= ND_PASS_PREV;
            if (was && (was->arg[0] & 0xFF) != (p.arg[0] & 0xFF)) {
                b.mode |= ND_PASS_PAIRED;
                b.prev_rgba = (was->arg[0] & 0xFF) << 24;
            }
        }
        break;
    default:
        break;
    }
    BufferCreateInfo info = {};
    info.domain = BufferDomain::Device;
    info.size = sizeof(b);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    nd.pass_record = gs->device->create_buffer(info, &b); // (held by a recording that binds it)
}

void nd_pass_draw(const RdDraw &d) {
    RdPass p;
    rd_pass_of(d, p);
    NdKey key = nd_key(d);
    gsn_stats.nd_draws++;
    bool between = interp.active;
    if (between) {
        flush_batch(); // (the PS2 path's before it: a batch of their own)
        nd_pass_between(key, p);
        nd.batch_arena = nd.batch_draws = nd.pass_record.get();
        nd.pass_kicking = true;
    } else if (!replaying) {
        nd.pass_now[key] = p;
    }
    nd_pass_tex_from = 0xFFFFFFFFu;
    if (between || replaying) {
        // (a picture between is drawn into the buffer shown, frame_interp.c's FRAME written so, and the PS2
        // path's TEX0 of the frame drawn written to read that one too, ad_write: so the pass's TEX0)
        nd_pass_tex_from = p.pass == RD_PASS_PREVIOUS ? (p.frame_tbp ? 0 : 0x1180) : p.frame_tbp;
        nd_pass_tex_to = uint32_t(bits(gs->reg[R_FRAME_1], 0, 9)) * 32;
    }
    if (between && p.pass == RD_PASS_PREVIOUS) {
        snapshot.reads = true; // (frame_interp.c's marks 0xF4 and 0xF5 about the PS2 path's packet)
    }
    switch (p.pass) {
    case RD_PASS_SCALE:
        nd_pass_scale(p);
        break;
    case RD_PASS_FILL:
        nd_pass_fill(p.arg[0], p.arg[1], p.arg[2], p.arg[3], p.arg[4] | uint64_t(p.arg[5]) << 32);
        break;
    case RD_PASS_FADE:
        nd_pass_fade(p);
        break;
    case RD_PASS_FLICKER:
        nd_pass_flicker(p);
        break;
    case RD_PASS_PREVIOUS:
        nd_pass_previous(p);
        break;
    case RD_PASS_VIEW_SHIFT:
        nd_pass_view_shift(p);
        break;
    case RD_PASS_HAZE:
        nd_pass_haze(p);
        break;
    case RD_PASS_WOBBLE:
        nd_pass_wobble(p);
        break;
    default:
        gsn_stats.nd_unsupported++;
        break;
    }
    gsn_stats.nd_batches++;
    if (between) {
        nd.pass_kicking = false;
        flush_batch(); // (the pass's last corners: their batch is its own)
        nd.pass_pending = false;
        snapshot.reads = false;
        // (what it wrote is no kick's of the frame's)
        interp.st_src = interp.q_src = interp.rgba_src = interp.rgba_q_src = interp.uv_src = GSN_IM_NONE;
    }
    nd_pass_tex_from = 0xFFFFFFFFu;
}
