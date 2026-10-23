// ---- Native draw: the game's draws at its own level, hosted in the GS's stream ----------------------
// (docs/port/NATIVE_DRAW.md, "Hosted mode"; port/include/port/render.h.) Included by gsn.cpp, inside its
// namespace, before flush_batch: the native GS's own code, kept in a file of its own.
//
// The patched game queues, where it would have sent a model part's packets, a GS register write to
// PORT_GS_REG_NATIVE_DRAW (0x7B) naming a draw in the renderer core's side buffer: the part's meshes (its
// data blocks, parsed once and kept on the GPU) and what VU1's fixed block would have held for it. Met
// here, in order with the rest of the frame, it is drawn the way VU1 and the GS would have drawn the
// packets: for each batch (one VU1 run) the A+D registers the run kicks are written as the GS gets them
// (TEX0, TEX1, ALPHA ... and FOGCOL at the set-up), PRIM from the strip's tag, and then the batch's
// triangles go through flush_batch like any other batch: the same targets, texture cache, blending, Z
// and layer rules, render passes and recording for the pictures between. Only the vertices differ:
// shaders/nd_000.vert does what VU1's program 0x000 did (port/platform/vu1_hle.c is its specification),
// from the mesh's vertices on the GPU and the draw's constants.
//
// flush_batch decides much from the vertices (the rows a texture is read to, the scissor, which
// triangles overlap and so go in runs, the layers, the rows written). So the batch's triangles are also
// worked out here on the CPU, as far as their places go (the "plan": positions, Z, the clipping that
// gives VU1's ADC, alpha and ST from the data), and gs->verts holds them with, in each corner's fog, the
// vertex the shader is to make of it. What is drawn is the shader's.
//
// --native-draw-compare queues the PS2 path's packets as well, between RD_ND_ALT_BEGIN and _END after
// the native draws: the GS draws one of the two (gsn_set_native_draw), so two runs, identical in
// everything the game does, draw the frame each way.

void flush_batch();
void gs_write(uint32_t address, uint64_t value);

// nd_000.vert's Draw (std140): matrices are VU layout, a qword a column
struct NdConst {
    float world_screen[16];
    float model[16];
    float view[16];
    float screen[16];
    float light_dir[16];
    float light_col[16];
    float fog[4];
    float point_pos[16]; // in the model's space, as VU1's set-up leaves them; w the falloff
    float point_col[16];
    int32_t info[4];     // the point lights, XYOFFSET's X and Y (12.4), ND_C_* flags
    int32_t info2[4];    // the batch's first vertex in the mesh's buffer
};
static_assert(sizeof(NdConst) == 35 * 16, "NdConst is nd_000.vert's Draw");
enum : int32_t {
    ND_C_MOVING = 0x1, // a picture between: the camera moved by the picture's (set 1's Picture)
    ND_C_GOURAUD = 0x2, // PRIM's IIP: each corner its own colour (else the kick's)
};

const uint32_t N_ND_MAX = 40; // (a batch's vertices at most: RdBatch)

struct NdGpuMesh {
    BufferHandle buffer;
    uint64_t used;
};

struct NdState {
    bool active = false;   // a native batch is in flush_batch
    bool skipping = false; // between RD_ND_ALT_BEGIN and _END, drawing the native draws
    NdConst consts;
    const Buffer *buffer = nullptr;
    std::unordered_map<RdMesh, NdGpuMesh> meshes;
    std::unordered_map<Program *, Program *> programs; // the GS's programs, with nd_000.vert
    Shader *vs = nullptr;
    BufferHandle dummy;
    uint64_t frame = 0;
    std::vector<std::shared_ptr<const RdMeshData>> fetched;
} nd;
volatile int nd_mode = GSN_NATIVE_DRAW_NATIVE; // (gsn_set_native_draw)

// The program p (one of the GS's, its fragment shader) with nd_000.vert, while a native batch is drawn
Program *nd_program(Program *p) {
    if (!nd.active || !p) {
        return p;
    }
    auto it = nd.programs.find(p);
    if (it != nd.programs.end()) {
        return it->second;
    }
    if (!nd.vs) {
        ResourceLayout vert = {};
        vert.input_mask = 1u << 4;
        vert.push_constant_size = sizeof(Push);
        vert.sets[1].storage_buffer_mask = 1u;
        vert.sets[1].meta[0].array_size = 1;
        vert.sets[2].storage_buffer_mask = 1u;
        vert.sets[2].uniform_buffer_mask = 2u;
        vert.sets[2].meta[0].array_size = 1;
        vert.sets[2].meta[1].array_size = 1;
        nd.vs = gs->device->request_shader(gsn_spv_nd_000_vert, sizeof(gsn_spv_nd_000_vert), &vert);
    }
    Program *np = gs->device->request_program(nd.vs, p->get_shader(ShaderStage::Fragment));
    nd.programs[p] = np;
    return np;
}

// (flush_batch, a native batch's draw: its vertices and constants)
void nd_bind(Cmd &c) {
    c.set_storage_buffer(2, 0, *nd.buffer);
    void *k = c.allocate_constant_data(2, 1, sizeof(NdConst));
    memcpy(k, &nd.consts, sizeof(NdConst));
    if (!interp.active) {
        // (a picture between's Picture is bound by its kicks' pass, recorded; else nothing moves)
        if (!nd.dummy) {
            BufferCreateInfo info = {};
            info.domain = BufferDomain::Device;
            info.size = 256;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            std::vector<uint8_t> zeros(256);
            nd.dummy = gs->device->create_buffer(info, zeros.data());
        }
        c.set_storage_buffer(1, 0, *nd.dummy);
    }
}

const Buffer &nd_gpu_mesh(const RdMeshData &m) {
    NdGpuMesh &g = nd.meshes[m.id];
    g.used = nd.frame;
    if (!g.buffer) {
        BufferCreateInfo info = {};
        info.domain = BufferDomain::Device;
        info.size = std::max<VkDeviceSize>(m.vertices.size() * sizeof(int32_t), 16);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        g.buffer = gs->device->create_buffer(info, m.vertices.data());
        gsn_stats.nd_mesh_uploads++;
    }
    return *g.buffer;
}

// 4x4 products in VU layout (a qword a column): out = a b
void nd_mul(float *out, const float *a, const float *b) {
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
        }
    }
}

// The batch's triangles planned on the CPU into gs->verts (see the top): those VU1 would kick, each
// corner's fog the vertex nd_000.vert makes of it (its number, and the kick's << 8)
void nd_plan(const RdMeshData &m, const RdBatch &b, const float *vm, uint32_t prim) {
    uint64_t offset = gs->reg[R_XYOFFSET_1 + ((prim >> 9) & 1)];
    int32_t ox = int32_t(bits(offset, 0, 16)), oy = int32_t(bits(offset, 32, 16));
    Vtx v[N_ND_MAX];
    bool clip[N_ND_MAX];
    for (uint32_t k = 0; k < b.count; k++) {
        const int32_t *q = &m.vertices[(b.first + k) * 16];
        float p[4] = {float(q[0]), float(q[1]), float(q[2]), float(q[3])};
        float c[4];
        for (int r = 0; r < 4; r++) {
            c[r] = vm[r] * p[0] + vm[4 + r] * p[1] + vm[8 + r] * p[2] + vm[12 + r] * p[3];
        }
        float qq = 1.0f / c[3];
        float x = c[0] * qq, y = c[1] * qq, z = c[2] * qq;
        // VU1's clip judgement: past the guard band (2047 about the 2048 middle) in X or Y, or Z below 0
        clip[k] = !(fabsf(x - 2048.0f) <= 2047.0f && fabsf(y - 2048.0f) <= 2047.0f && !(z < 0.0f));
        int32_t xi = clip[k] ? 0 : int32_t(x * 16.0f), yi = clip[k] ? 0 : int32_t(y * 16.0f);
        int32_t zi = clip[k] ? 0 : int32_t(std::min(z * 16.0f, 2.0e9f));
        v[k].x = float(int32_t(uint32_t(xi) & 0xFFFF) - ox) / 16.0f;
        v[k].y = float(int32_t(uint32_t(yi) & 0xFFFF) - oy) / 16.0f;
        v[k].z = uint32_t(zi >> 4) & 0xFFFFFF;
        int32_t a = q[15];
        v[k].rgba = 0x00808080u | uint32_t(a < 0 ? 0 : a > 255 ? 255 : a) << 24;
        v[k].s = float(q[8]) / 4096.0f * qq;
        v[k].t = float(q[9]) / 4096.0f * qq;
        v[k].q = float(q[10]) / 4096.0f * qq;
    }
    for (size_t i = 0; i + 2 < b.triangles.size(); i += 3) {
        uint32_t t[3] = {b.triangles[i], b.triangles[i + 1], b.triangles[i + 2]};
        uint32_t kick = t[2];
        // (VU1's ADC: the kick's clip and the two kicks' before it, whichever vertices the triangle has)
        if (clip[kick] || (kick >= 1 && clip[kick - 1]) || (kick >= 2 && clip[kick - 2])) {
            continue;
        }
        for (int j = 0; j < 3; j++) {
            Vtx w = v[t[j]];
            w.fog = float(t[j] | kick << 8);
            if (!((prim >> 3) & 1)) {
                w.rgba = v[kick].rgba; // (flat: the kick's colour, as add_triangle has it)
            }
            gs->verts.push_back(w);
            if (interp.active) {
                interp.vsrc.push_back({GSN_IM_NONE, GSN_IM_NONE, GSN_IM_NONE, uint32_t(j)});
            }
        }
    }
}

void nd_draw(uint64_t value) {
    RdDraw d;
    RdCamera cam;
    if (!rd_fetch(value, d, cam, nd.fetched)) {
        gsn_stats.nd_lost++;
        return;
    }
    if (d.program != 0) {
        gsn_stats.nd_unsupported++;
        return;
    }
    flush_batch();
    gsn_stats.nd_draws++;
    NdConst &k = nd.consts;
    memcpy(k.world_screen, cam.world_screen, 64);
    memcpy(k.model, d.model, 64);
    memcpy(k.view, cam.view, 64);
    memcpy(k.screen, cam.screen, 64);
    memcpy(k.light_dir, d.light_dir, 64);
    memcpy(k.light_col, d.light_col, 64);
    memcpy(k.fog, d.fog, 16);
    // VU1's set-up: the point lights into the model's space. Less the model's translation, then by its
    // rotation with the off-diagonal signs turned (the set-up's own maths: the transpose only for a
    // turn about one axis, as the game's models are)
    int32_t lights = std::max(0, std::min(4, int32_t(int16_t(d.point_count))));
    for (int l = 0; l < 4; l++) {
        const float *m = &d.model[0][0];
        float p[3] = {d.point_pos[l][0] - m[12], d.point_pos[l][1] - m[13], d.point_pos[l][2] - m[14]};
        float r0[3] = {m[0], -m[1], -m[2]}, r1[3] = {-m[4], m[5], -m[6]}, r2[3] = {-m[8], -m[9], m[10]};
        for (int i = 0; i < 3; i++) {
            k.point_pos[l * 4 + i] = r0[i] * p[0] + r1[i] * p[1] + r2[i] * p[2];
        }
        k.point_pos[l * 4 + 3] = d.point_pos[l][3];
        memcpy(&k.point_col[l * 4], d.point_col[l], 16);
    }
    float vm[16];
    nd_mul(vm, &cam.world_screen[0][0], &d.model[0][0]);
    for (const auto &mesh : nd.fetched) {
        const Buffer &buffer = nd_gpu_mesh(*mesh);
        for (const RdBatch &b : mesh->batches) {
            if (b.start != RD_PROGRAM_CONTINUE) {
                gs_write(R_FOGCOL, d.fogcol); // (an MSCAL: the set-up kicks FOGCOL, VU1 0x3D)
            }
            for (size_t i = 0; i + 1 < b.ad.size(); i += 2) {
                gs_write(uint32_t(b.ad[i]), b.ad[i + 1]);
            }
            uint32_t prim = uint32_t((b.tag[0] >> 47) & 0x7FF);
            gs_write(R_PRIM, prim);
            uint32_t eprim = effective_prim();
            uint64_t offset = gs->reg[R_XYOFFSET_1 + ((eprim >> 9) & 1)];
            k.info[0] = lights;
            k.info[1] = int32_t(bits(offset, 0, 16));
            k.info[2] = int32_t(bits(offset, 32, 16));
            k.info[3] = (interp.active ? ND_C_MOVING : 0) | (((eprim >> 3) & 1) ? ND_C_GOURAUD : 0);
            k.info2[0] = int32_t(b.first);
            k.info2[1] = k.info2[2] = k.info2[3] = 0;
            gs->batch_prim = eprim;
            gs->batch_sprites = false;
            nd_plan(*mesh, b, vm, eprim);
            gsn_stats.nd_batches++;
            nd.buffer = &buffer;
            nd.active = true;
            flush_batch();
            nd.active = false;
            nd.buffer = nullptr;
        }
    }
    // (the GS's vertex queue as the strip left it does not matter: the next draw writes PRIM)
    gs->queue_count = 0;
    nd.fetched.clear();
}

// PORT_GS_REG_NATIVE_DRAW
void nd_register(uint64_t value) {
    switch (RD_ND_COMMAND(value)) {
    case RD_ND_DRAW:
        if (nd_mode == GSN_NATIVE_DRAW_NATIVE) {
            nd_draw(value);
        }
        break;
    case RD_ND_ALT_BEGIN:
        flush_batch();
        nd.skipping = nd_mode == GSN_NATIVE_DRAW_NATIVE;
        break;
    case RD_ND_ALT_END:
        nd.skipping = false;
        break;
    case RD_ND_FRAME:
        rd_consumer_frame(value);
        break;
    }
}

// A register write between RD_ND_ALT_BEGIN and _END while the native draws are drawn: left out, but a
// vertex kick still counts among the frame's kicks (a picture between's moves are one a kick)
void nd_skip(uint32_t address) {
    gsn_stats.nd_skipped++;
    if (interp.active && (address == R_XYZF2 || address == R_XYZ2 || address == R_XYZF3 || address == R_XYZ3)) {
        if (interp.kick < interp.frame->kicks) {
            interp.kick_list[interp.kick] = InterpKick{};
        }
        interp.kick++;
    }
}

// At each vblank: GPU meshes no draw has named for a while go
void nd_vblank() {
    nd.frame++;
    if (nd.frame % 256 != 0 || nd.meshes.size() < 1024) {
        return;
    }
    for (auto it = nd.meshes.begin(); it != nd.meshes.end();) {
        if (nd.frame - it->second.used > 1024) {
            it = nd.meshes.erase(it);
        } else {
            ++it;
        }
    }
}
