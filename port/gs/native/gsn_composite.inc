// ---- Standalone (P8 T2): the scene at a scale of its own, and the composite -----------------------------
// (docs/port/NATIVE_DRAW_P8.md, T2.) Included by gsn.cpp after gsn_standalone.inc, inside its namespace.
//
// With --scene-scale (and --scene-msaa) the scene's targets have a scale and samples of their own, apart from the
// GS's (--native-scale, --native-msaa): the world can be drawn at 4x with 4 samples while the GS draws the HUD, the
// messages, the menus and the title at 1x, as the PS2 did. The scene gives the frame to the GS at the GS's scale
// (sa_give: each GS pixel the mean of the scene's pixels and samples over it), and the GS draws what follows the
// world over it as it always has, at its own scale, with its own arithmetic (the 2D, the fades, the brightness).
// The picture shown is then made from both (sa_composite, at the scanout): the scene's picture where the GS drew
// nothing over the world but colours over the whole screen, the GS's where it drew the 2D.
//
// Why not a transparent layer. The plan had the GS draw its 2D into a transparent layer, blended over the scene
// (premultiplied). The GS's 2D needs the frame's own alpha (the MP orb is drawn through a mask of destination
// alpha: FBA, then DATE), and what it draws after the world includes passes that read the whole frame (the
// brightness, last, over the HUD too; the tint under water) and fills over it (the fades, the message's dim, the
// pause): a layer's colour and coverage cannot carry those. Instead:
// - the scene's picture is kept as it was given (sa_hold: hi, the scene's, its samples resolved; lo, the GS's frame
//   just after, at the GS's scale);
// - each batch the GS then draws over that frame (sa_note_gs_batch) that is one colour over the whole screen is
//   kept in order, the chain: a flat colour blended over it (a fill, a fade, the pause's shade: ALPHA with As or
//   FIX), set over it (no blending), or the frame read as a texture a texel a pixel and modulated by a colour (the
//   brightness, the tint);
// - at the scanout each pixel of the picture looks at its GS pixel: where the GS's frame is what the chain makes of
//   lo (within a level or two: Vulkan's blending rounds as it rounds), nothing else was drawn there, and the
//   pixel is the scene's own, through the chain (with the GS's integer arithmetic); anywhere else the GS's 2D is
//   over it, and the pixel is the GS's (its GS pixel repeated, as the PS2's 2D is). Under the 2D's see-through
//   parts the world is the GS's scale.
// A frame kept is good for the vblanks that show it (the frame's own and the one after, and the pictures between
// them); the GS's clear of that buffer for the next frame, or a fourth vblank, lets it go.

struct SaHeld {
    bool valid = false;
    uint32_t fbp = 0, fbw = 0, fpsm = 0;
    const Target *t = nullptr;
    uint32_t sx = 1, sy = 1;   // the scene's scale
    uint32_t gsx = 1, gsy = 1; // the GS target's
    uint32_t rows = 0;         // the frame's rows the world was drawn in (GS pixels: the scissor's)
    ImageHandle hi, lo;
    uint32_t ops[8][4] = {};
    int count = 0;
    uint64_t vblank = 0;
};
std::vector<SaHeld> sa_held;
Program *sa_program_composite = nullptr;

SaHeld *sa_held_of(uint32_t fbp, uint32_t fbw, uint32_t fpsm, bool make) {
    for (SaHeld &h : sa_held) {
        if (h.fbp == fbp && h.fbw == fbw && h.fpsm == fpsm) {
            return &h;
        }
    }
    if (!make) {
        return nullptr;
    }
    if (sa_held.size() >= 4) {
        sa_held.erase(sa_held.begin());
    }
    sa_held.emplace_back();
    SaHeld &h = sa_held.back();
    h.fbp = fbp;
    h.fbw = fbw;
    h.fpsm = fpsm;
    return &h;
}

void sa_held_forget() {
    sa_held.clear();
}

// (sa_give with --upscaler) A frame given at the GS's own scale: no composite of that buffer from before
void sa_held_drop(uint32_t fbp, uint32_t fbw, uint32_t fpsm) {
    if (SaHeld *h = sa_held_of(fbp, fbw, fpsm, false)) {
        h->valid = false;
    }
}

// (sa_give, a frame given back for good at another scale or samples than the GS's) The scene's picture kept, and
// the GS's frame as it was given it
void sa_hold(Target &color) {
    SaHeld *h = sa_held_of(sa.fbp, sa.fbw, sa.fpsm, true);
    uint32_t W = sa.color->get_width(), H = sa.color->get_height();
    uint32_t gw = color.image->get_width(), gh = color.image->get_height();
    if (!h->hi || h->hi->get_width() != W || h->hi->get_height() != H || h->hi->get_format() != sa.color->get_format()) {
        h->hi = sa_image(W, H, sa.color->get_format(), 1);
    }
    if (!h->lo || h->lo->get_width() != gw || h->lo->get_height() != gh || h->lo->get_format() != color.image->get_format()) {
        h->lo = sa_image(gw, gh, color.image->get_format(), 1);
    }
    {
        Why why(GSN_WHY_COPY);
        end_pass();
    }
    gpu_mark(GPU_OTHER);
    if (sa.samples > 1) {
        msaa_resolve(*h->hi, *sa.color, {0, 0, 0}, {W, H, 1});
    } else {
        transfer_barrier();
        cmd().copy_image(*h->hi, *sa.color, {0, 0, 0}, {0, 0, 0}, {W, H, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
        transfer_barrier();
    }
    {
        Why why(GSN_WHY_COPY);
        msaa_settle(color);
        end_pass();
    }
    transfer_barrier();
    cmd().copy_image(*h->lo, *color.image, {0, 0, 0}, {0, 0, 0}, {gw, gh, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    transfer_barrier();
    gpu_mark(GPU_COPY);
    gsn_stats.copy_bytes += (uint64_t(W) * H + uint64_t(gw) * gh) * (wide(*h->lo) ? 8 : 4);
    h->t = &color;
    h->sx = sa.sx;
    h->sy = sa.sy;
    h->gsx = color.sx;
    h->gsy = color.sy;
    h->rows = std::max(sa.scissor_rows, 1u);
    h->count = 0;
    h->valid = true;
    h->vblank = sa_vblanks;
}

// (sa_flush, a batch the GS draws) The GS's clear of a frame (libgraph's, at the swap: untextured sprites of one
// colour and Z over the whole frame, Z always passing and written): the scene may take that frame by clearing its
// own (sa.clear), and a frame kept of that buffer is let go. Then, of a frame the scene gave back (kept: sa_hold),
// the batches drawn over the whole of it in one colour: the composite's chain
void sa_note_gs_batch() {
    if (gs->verts.empty()) {
        return;
    }
    uint32_t prim = gs->batch_prim, ctxt = (prim >> 9) & 1;
    bool tme = (prim >> 4) & 1, abe = (prim >> 6) & 1, fst = (prim >> 8) & 1;
    uint64_t frame = gs->reg[R_FRAME_1 + ctxt], zbuf = gs->reg[R_ZBUF_1 + ctxt], test = gs->reg[R_TEST_1 + ctxt];
    uint64_t scissor = gs->reg[R_SCISSOR_1 + ctxt], alpha = gs->reg[R_ALPHA_1 + ctxt];
    uint64_t tex0 = gs->reg[R_TEX0_1 + ctxt], tex1 = gs->reg[R_TEX1_1 + ctxt];
    uint32_t fbp = bits(frame, 0, 9) * 32, fbw = bits(frame, 16, 6), fpsm = bits(frame, 24, 6);
    uint32_t zbp = bits(zbuf, 0, 9) * 32, zpsm = bits(zbuf, 24, 4) | 0x30;
    uint32_t rows = bits(scissor, 48, 11) + 1;
    bool zte = bits(test, 16, 1), date = bits(test, 14, 1);
    uint32_t ztst = zte ? bits(test, 17, 2) : 1;
    bool ate = bits(test, 0, 1) && bits(test, 1, 3) != 1;
    bool from_origin = bits(scissor, 0, 11) == 0 && bits(scissor, 32, 11) == 0;
    uint32_t rgba = 0;
    if (!tme && !abe && zte && ztst == 1 && !bits(zbuf, 32, 1) && !date && !ate && uint32_t(frame >> 32) == 0 && from_origin &&
        bits(scissor, 16, 11) + 1 >= fbw * 64 && sa_sprites_cover(fbw, rows, &rgba)) {
        bool one_z = true;
        for (const Vtx &v : gs->verts) {
            one_z &= v.z == gs->verts[0].z;
        }
        if (one_z) {
            sa.clear = {true, fbp, fbw, fpsm, zbp, zpsm, rgba, gs->verts[0].z, rows};
            if (SaHeld *h = sa_held_of(fbp, fbw, fpsm, false)) {
                h->valid = false;
            }
            if (sa.log) {
                fprintf(stderr, "[sa] the GS's clear: fbp %#x zbp %#x colour %#x z %u, %u rows\n", fbp, zbp, rgba,
                        gs->verts[0].z, rows);
            }
            return;
        }
    }
    const Sa::Clear &k = sa.clear;
    if (k.valid && sa_batch_touches(k.fbp, k.fbw, k.fpsm, k.zbp, k.zpsm, k.rows)) {
        sa.clear.valid = false;
        if (sa.log) {
            fprintf(stderr, "[sa] the GS's clear drawn over: prim %#x frame %#llx zbuf %#llx test %#llx tex0 %#llx n %zu\n", prim,
                    (unsigned long long)frame, (unsigned long long)zbuf, (unsigned long long)test, (unsigned long long)tex0,
                    gs->verts.size());
        }
    }
    SaHeld *h = sa_held_of(fbp, fbw, fpsm, false);
    if (!h || !h->valid || (sa.bound && sa.fbp == fbp && sa.fbw == fbw && sa.fpsm == fpsm)) {
        return;
    }
    // one colour over every pixel of the frame: its RGB written whole, no DATE, Z not tested, the alpha test
    // passing, or failing into the frame all the same (AFAIL FB_ONLY, RGB_ONLY: the game's fills test NEVER so),
    // the sprites over the whole of the world's rows
    if ((uint32_t(frame >> 32) & 0xFFFFFF) || date || ztst != 1 || !from_origin || !sa_sprites_cover(fbw, h->rows, &rgba)) {
        return;
    }
    if (ate) {
        uint32_t afail = bits(test, 12, 2);
        if (afail != 1 && afail != 3) {
            int as = int(rgba >> 24), ref = int(bits(test, 4, 8));
            bool pass = false;
            switch (bits(test, 1, 3)) {
            case 2: pass = as < ref; break;
            case 3: pass = as <= ref; break;
            case 4: pass = as == ref; break;
            case 5: pass = as >= ref; break;
            case 6: pass = as > ref; break;
            case 7: pass = as != ref; break;
            default: break;
            }
            if (tme || !pass) {
                return; // (each texel its own, or nothing written)
            }
        }
    }
    uint32_t op[4] = {0, rgba, 0, 0};
    if (!tme) {
        if (!abe) {
            op[0] = 3; // (set)
        } else {
            uint32_t a = bits(alpha, 0, 2), b = bits(alpha, 2, 2), c = bits(alpha, 4, 2), d = bits(alpha, 6, 2);
            if (c == 1 || a == 3 || b == 3 || d == 3 || bits(gs->reg[R_PABE], 0, 1)) {
                return; // (by the frame's own alpha, or each pixel its own: not one colour over all)
            }
            op[0] = 2 | (bits(gs->reg[R_COLCLAMP], 0, 1) << 8);
            op[2] = a | b << 2 | c << 4 | d << 6;
            op[3] = bits(alpha, 32, 8);
        }
    } else {
        // the frame itself read a texel a pixel (FST, nearest), modulated by the colour, no blending: the brightness,
        // the tint
        uint32_t tbp = bits(tex0, 0, 14), tbw = bits(tex0, 14, 6), tpsm = bits(tex0, 20, 6), tfx = bits(tex0, 35, 2);
        uint32_t ba = bits(alpha, 0, 2), bb = bits(alpha, 2, 2), bd = bits(alpha, 6, 2);
        if (tbp != fbp || tbw != fbw || tpsm != fpsm || tfx != 0 || !fst || bits(tex1, 0, 1) || (abe && !(ba == bb && bd == 0))) {
            return;
        }
        // (each pixel its own texel: a corner's texel at its place, or less than one on where the sprite's edge
        // is half a pixel short of its texels', as the game's 447.5 rows over 448 texels)
        for (const Vtx &v : gs->verts) {
            float ds = v.s - v.x, dt = v.t - v.y;
            if (ds < -1.0f / 64.0f || dt < -1.0f / 64.0f || ds > 0.5f || dt > 0.5f || (v.x <= 0.0f && fabsf(ds) > 1.0f / 64.0f) ||
                (v.y <= 0.0f && fabsf(dt) > 1.0f / 64.0f)) {
                return;
            }
        }
        op[0] = 1;
    }
    if (h->count >= 8) {
        h->valid = false; // (more than the composite takes: the GS's picture, as it is)
        return;
    }
    memcpy(h->ops[h->count++], op, sizeof(op));
}

// (scanout_to_image, a whole picture) Whether the picture of the shown buffer is the composite's (a frame the scene
// drew at a scale of its own, kept and still good), and its size
const SaHeld *sa_composite_of(const Shown &s) {
    if (!sa.on || !s.t) {
        return nullptr;
    }
    const SaHeld *h = sa_held_of(s.fbp, s.fbw, s.psm, false);
    if (!h || !h->valid || h->t != s.t || sa_vblanks - h->vblank > 3 || !h->lo || s.t->image->get_width() != h->lo->get_width() ||
        s.t->image->get_height() != h->lo->get_height() || s.t->image->get_format() != h->lo->get_format() ||
        (h->sx == s.sx && h->sy == s.sy) || h->gsx != s.sx || h->gsy != s.sy ||
        size_t(s.w) * h->sx * s.h * h->sy > PORT_GS_FRAME_PIXELS || (s.dbx + s.w) * h->sx > h->hi->get_width() ||
        (s.dby + s.h) * h->sy > h->hi->get_height()) {
        return nullptr;
    }
    return h;
}
bool sa_composite_size(const Shown &s, uint32_t *iw, uint32_t *ih) {
    const SaHeld *h = sa_composite_of(s);
    if (!h) {
        return false;
    }
    if (iw) {
        *iw = s.w * h->sx;
        *ih = s.h * h->sy;
    }
    return true;
}

// The composite's picture into dst (as large as sa_composite_size says): shaders/sa_composite.frag
void sa_composite(const Image &dst, const Shown &s) {
    const SaHeld *h = sa_composite_of(s);
    if (!sa_program_composite) {
        ResourceLayout vert = {};
        ResourceLayout frag = {};
        frag.output_mask = 1;
        frag.push_constant_size = 32;
        frag.sets[0].sampled_image_mask = 7;
        frag.sets[0].fp_mask = 7;
        frag.sets[0].uniform_buffer_mask = 1u << 3;
        for (int b = 0; b < 4; b++) {
            frag.sets[0].meta[b].array_size = 1;
        }
        sa_program_composite = gs->device->request_program(
            gs->device->request_shader(gsn_spv_msaa_vert, sizeof(gsn_spv_msaa_vert), &vert),
            gs->device->request_shader(gsn_spv_sa_composite_frag, sizeof(gsn_spv_sa_composite_frag), &frag));
    }
    msaa_settle(*s.t);
    end_pass();
    RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = &dst.get_view();
    rp.store_attachments = 1;
    Cmd &c = cmd();
    c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    c.begin_render_pass(rp);
    c.set_opaque_state();
    c.set_program(sa_program_composite);
    c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    c.set_cull_mode(VK_CULL_MODE_NONE);
    c.set_depth_test(false, false);
    c.set_texture(0, 0, s.t->image->get_view(), StockSampler::NearestClamp);
    c.set_texture(0, 1, h->lo->get_view(), StockSampler::NearestClamp);
    c.set_texture(0, 2, h->hi->get_view(), StockSampler::NearestClamp);
    uint32_t *chain = c.allocate_typed_constant_data<uint32_t>(0, 3, 32);
    memset(chain, 0, 32 * sizeof(uint32_t));
    memcpy(chain, h->ops, sizeof(uint32_t) * 4 * size_t(h->count));
    struct {
        float to_gs[2];
        int32_t gs_origin[2], hi_origin[2];
        int32_t count;
        float tol;
    } push = {{float(h->gsx) / float(h->sx), float(h->gsy) / float(h->sy)},
              {int32_t(s.dbx * h->gsx), int32_t(s.dby * h->gsy)},
              {int32_t(s.dbx * h->sx), int32_t(s.dby * h->sy)},
              h->count,
              2.0f};
    c.push_constants(&push, 0, sizeof(push));
    c.draw(3);
    c.end_render_pass();
    c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    sa_stats.composites++;
    sa_stats.composite_ops += uint64_t(h->count);
}
