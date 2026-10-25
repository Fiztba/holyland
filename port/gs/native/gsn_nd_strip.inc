// ---- Native draw: a PATH2 strip, the weapon's trail (P2-4) -------------------------------------------
// Included at the end of gsn_native_draw.inc (inside gsn.cpp's namespace). A draw of program
// RD_PROGRAM_GS_STRIP names meshes of RD_VF_GS_STRIP (render.cpp, parse_gs_strip): a GIF packet the game
// built on the CPU, a PACKED triangle strip of RGBAQ and XYZ2 (Player_DrawArm's trail, handed over by
// game/native_draw/nd_arm.c with the arm's identity). No VU1 program is involved: it is written to the GS
// as gif() writes the packet (PRE's PRIM, Q set to 1 by the tag, then each vertex's RGBAQ and XYZ2 or,
// with the ADC bit, XYZ3), so the GS's state and batches are the PS2 path's. What a draw of its own gives
// is the identity, for the pictures between and the standalone renderer.
//
// Pictures between (P2-5). The strip's points are screen points (WeaponTrail_Update projects them), so it
// is drawn as a 2D item: its points and colours t of the way from their partners in the frame before's
// strip of the same identity (nd_strip_between: the points it shares with it stay, its new head grows out
// of the old head; the PS2 path pairs only a strip of as many points, so its trail jumps a pair ahead at
// each frame while it grows), else as the frame has them. Its vertex kicks are not the frame's (frame_interp.c never saw them: they are not in its
// moves, and a kick counted would put every later one out by one), so in a picture between the strip's
// triangles are made here and drawn by shaders/nd_strip.vert, which takes the frame before's place and
// colour from the corner's S, T and Q (the trail is not textured).

void packed(uint32_t desc, const uint64_t q[2]);

// A strip vertex's place (X | Y << 16, 12.4) and RGBA, as packed() takes them from the mesh's two qwords
uint32_t nd_strip_xy(const int32_t *v) {
    return (uint32_t(v[4]) & 0xFFFF) | (uint32_t(v[5]) & 0xFFFF) << 16;
}
uint32_t nd_strip_rgba(const int32_t *v) {
    return (uint32_t(v[0]) & 0xFF) | (uint32_t(v[1]) & 0xFF) << 8 | (uint32_t(v[2]) & 0xFF) << 16 | (uint32_t(v[3]) & 0xFF) << 24;
}

// The program p (one of the GS's, its fragment shader) with nd_strip.vert, while a strip is drawn in a
// picture between
Program *nd_strip_program(Program *p) {
    auto &programs = nd.strip_programs;
    Shader *&vs = nd.strip_vs;
    auto it = programs.find(p);
    if (it != programs.end()) {
        return it->second;
    }
    if (!vs) {
        ResourceLayout vert = {};
        vert.input_mask = 0x1F;
        vert.push_constant_size = sizeof(Push);
        vert.sets[1].storage_buffer_mask = 1u;
        vert.sets[1].meta[0].array_size = 1;
        vs = gs->device->request_shader(gsn_spv_nd_strip_vert, sizeof(gsn_spv_nd_strip_vert), &vert);
    }
    Program *np = gs->device->request_program(vs, p->get_shader(ShaderStage::Fragment));
    programs[p] = np;
    return np;
}

// In a picture between: the strip's triangles, each corner with the frame before's place and colour
void nd_strip_between(const NdKey &key) {
    std::vector<uint32_t> xy;
    for (const auto &mesh : nd.fetched) {
        for (const RdBatch &b : mesh->batches) {
            for (uint32_t k = 0; k < b.count; k++) {
                xy.push_back(nd_strip_xy(&mesh->vertices[size_t(b.first + k) * 8]));
            }
        }
    }
    // Each point's partner in the frame before (from[i]). The trail is a ring of the blade's two ends,
    // pushed a pair a frame and kept where they were on the screen until they die: the frame before's
    // points the same, from some point on (the oldest gone), then this frame's new pair, which grows out
    // of the frame before's last pair. Otherwise, as frame_interp.c pairs the PS2 path's: point by point,
    // as many as the frame before had, none more than 128 pixels away.
    auto it = nd.strip_before.find(key);
    std::vector<uint32_t> from(xy.size());
    bool paired = false;
    if (it != nd.strip_before.end() && !it->second.xy.empty()) {
        const std::vector<uint32_t> &was = it->second.xy;
        size_t n = was.size();
        for (size_t o = 0; o + 2 <= n && !paired && n - o <= xy.size(); o++) {
            size_t overlap = n - o;
            paired = overlap % 2 == 0 && xy.size() - overlap <= 2 && memcmp(&xy[0], &was[o], overlap * 4) == 0;
            for (size_t i = 0; paired && i < xy.size(); i++) {
                from[i] = uint32_t(i < overlap ? i + o : n - 2 + (i - overlap) % 2);
            }
        }
        if (!paired && n == xy.size()) {
            paired = true;
            for (size_t i = 0; paired && i < xy.size(); i++) {
                int32_t dx = int32_t(xy[i] & 0xFFFF) - int32_t(was[i] & 0xFFFF);
                int32_t dy = int32_t(xy[i] >> 16) - int32_t(was[i] >> 16);
                paired = dx >= -2048 && dx <= 2048 && dy >= -2048 && dy <= 2048;
                from[i] = uint32_t(i);
            }
        }
    }
    gsn_stats.nd_moved_strips += paired;
    size_t at = 0;
    for (const auto &mesh : nd.fetched) {
        if (mesh->format != RD_VF_GS_STRIP) {
            gsn_stats.nd_unsupported++;
            continue;
        }
        gsn_stats.nd_draws++;
        for (const RdBatch &b : mesh->batches) {
            gs_write(R_PRIM, (b.tag[0] >> 47) & 0x7FF);
            if (!gs->verts.empty()) {
                flush_batch(); // (a batch of its own, drawn by nd_strip.vert)
            }
            uint32_t prim = effective_prim();
            gs->batch_prim = prim;
            gs->batch_sprites = false;
            bool gouraud = (prim >> 3) & 1, lerp = paired && !((prim >> 4) & 1);
            uint64_t offset = gs->reg[R_XYOFFSET_1 + ((prim >> 9) & 1)];
            int32_t ox = int32_t(bits(offset, 0, 16)), oy = int32_t(bits(offset, 32, 16));
            float fog = float(bits(gs->reg[R_FOG], 56, 8));
            Vtx queue[3];
            int queued = 0;
            for (uint32_t k = 0; k < b.count; k++, at++) {
                const int32_t *v = &mesh->vertices[size_t(b.first + k) * 8];
                uint32_t was_xy = lerp ? it->second.xy[from[at]] : xy[at];
                uint32_t rgba = nd_strip_rgba(v), was_rgba = lerp ? it->second.rgba[from[at]] : rgba;
                Vtx w;
                w.x = float(int32_t(xy[at] & 0xFFFF) - ox) / 16.0f;
                w.y = float(int32_t(xy[at] >> 16) - oy) / 16.0f;
                w.z = uint32_t(v[6]);
                w.rgba = rgba;
                w.s = float(int32_t(was_xy & 0xFFFF) - ox) / 16.0f;
                w.t = float(int32_t(was_xy >> 16) - oy) / 16.0f;
                memcpy(&w.q, &was_rgba, 4);
                w.fog = fog;
                // (a strip, as vertex_kick queues it; a vertex with ADC (XYZ3) draws no triangle)
                if (queued == 3) {
                    queue[0] = queue[1];
                    queue[1] = queue[2];
                    queued = 2;
                }
                queue[queued++] = w;
                if (queued == 3 && !((uint32_t(v[7]) >> 15) & 1)) {
                    for (int j = 0; j < 3; j++) {
                        Vtx c = queue[j];
                        if (!gouraud) {
                            c.rgba = queue[2].rgba; // (flat: the last vertex's colour, as add_triangle)
                            c.q = queue[2].q;
                        }
                        gs->verts.push_back(c);
                        interp.vsrc.push_back({GSN_IM_NONE, GSN_IM_NONE, GSN_IM_NONE, uint32_t(j)});
                    }
                }
            }
            if (!gs->verts.empty()) {
                nd.pending = true;
                nd.strip_pending = true;
                nd.batch_arena = nd.batch_draws = nullptr; // (none read: nd_bind binds nothing)
                flush_batch();
            }
            gsn_stats.nd_batches++;
        }
    }
}

void nd_strip_draw(const RdDraw &d) {
    NdKey key = nd_key(d);
    if (interp.active) {
        nd_strip_between(key);
        gs->queue_count = 0;
        return;
    }
    NdStripPoints &now = nd.strip_now[key];
    now.xy.clear();
    now.rgba.clear();
    for (const auto &mesh : nd.fetched) {
        if (mesh->format != RD_VF_GS_STRIP) {
            gsn_stats.nd_unsupported++;
            continue;
        }
        gsn_stats.nd_draws++;
        for (const RdBatch &b : mesh->batches) {
            // (gif(), meeting the tag)
            gs_write(R_PRIM, (b.tag[0] >> 47) & 0x7FF);
            gs->internal_q = 0x3F800000u;
            interp.q_src = GSN_IM_NONE;
            for (uint32_t k = 0; k < b.count; k++) {
                const int32_t *v = &mesh->vertices[size_t(b.first + k) * 8];
                uint64_t q[2];
                memcpy(q, v, 16);
                packed(0x1, q); // RGBAQ
                memcpy(q, v + 4, 16);
                packed(0x5, q); // XYZ2 (XYZ3 with ADC)
                now.xy.push_back(nd_strip_xy(v));
                now.rgba.push_back(nd_strip_rgba(v));
            }
            gsn_stats.nd_batches++;
        }
    }
}
