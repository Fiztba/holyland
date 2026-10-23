// ---- Native draw's vertex oracle (gsn_nd_oracle.h) ---------------------------------------------------
// Included at the end of gsn_native_draw.inc. nd_000.vert built with ND_ORACLE (tools/build_shaders.py's
// VARIANTS) writes, for each corner, what it gives the GS into a buffer (set 2, binding 2), so the shader
// that draws is the one checked: the same source, the same maths, only the stores added. The corners are
// drawn as triangles into a target of one pixel that nobody reads, on a command buffer of the oracle's
// own, after the GS's thread has done all it was sent (ring_idle), and waited for.

void ring_idle();

} // namespace (gsn.cpp's: its C entry points below are outside it)
#include "gsn_nd_oracle.h"
namespace {

// A run's record as the shader takes it: an MSCAL's made by the CPU's set-up from the fixed block (as
// nd_draw makes a draw's from the game's), an MSCNT's from the registers the set-up left (VU1 reads them
// so); the fog and, for an MSCNT, the point lights from memory, where VU1 reads them each run
void nd_oracle_const(NdConst &k, const GsnNdOracleDraw &o) {
    memset(&k, 0, sizeof(k));
    if (o.mscal) {
        RdDraw d = {};
        RdCamera cam = {};
        memcpy(cam.world_screen, o.mem[0x00], 64);
        memcpy(d.model, o.mem[0x04], 64);
        memcpy(d.light_dir, o.mem[0x0C], 64);
        memcpy(d.light_col, o.mem[0x10], 64);
        int32_t w;
        memcpy(&w, &o.mem[0x40][3], 4);
        d.point_count = w;
        memcpy(d.point_pos, o.mem[0x41], 64);
        memcpy(d.point_col, o.mem[0x45], 64);
        nd_const(k, d, cam);
    } else {
        memcpy(k.vertex, o.vf[1], 64);
        memcpy(k.light_dir, o.vf[5], 48);
        memcpy(k.light_col, o.vf[8], 64);
        memcpy(k.point_pos, o.mem[0x4A], 64);
        memcpy(k.point_col, o.mem[0x4E], 64);
        int32_t w;
        memcpy(&w, &o.mem[0x49][3], 4);
        k.info[0] = std::max(0, std::min(4, int32_t(int16_t(w))));
    }
    memcpy(k.fog, o.mem[0x3C], 16);
}

} // namespace

extern "C" int gsn_nd_oracle_ready(void) {
    return gs && gs->device && gs->device->get_device_features().enabled_features.vertexPipelineStoresAndAtomics;
}

extern "C" int gsn_nd_oracle_run(const GsnNdOracleDraw *draws, uint32_t draw_count, const int32_t (*vertices)[16],
                                 const uint32_t *vertex_draw, uint32_t vertex_count, GsnNdOracleSetup *setups,
                                 GsnNdOracleVertex *out) {
    if (!gsn_nd_oracle_ready() || draw_count == 0 || draw_count > ND_DRAWS_MAX || vertex_count == 0) {
        return 0;
    }
    ring_idle();
    Device &dev = *gs->device;
    // the records, and the CPU's set-up and plan
    std::vector<NdConst> consts(draw_count);
    for (uint32_t i = 0; i < draw_count; i++) {
        nd_oracle_const(consts[i], draws[i]);
        memset(&setups[i], 0, sizeof(setups[i]));
        if (draws[i].mscal) {
            memcpy(setups[i].vertex, consts[i].vertex, 64);
            memcpy(setups[i].light_dir, consts[i].light_dir, 48);
            memcpy(setups[i].point_pos, consts[i].point_pos, 64);
        }
    }
    for (uint32_t i = 0; i < vertex_count; i++) {
        bool clip;
        memset(&out[i], 0, sizeof(out[i]));
        nd_plan_vertex(vertices[i], consts[vertex_draw[i]].vertex, out[i].plan_xyz, out[i].plan_stq, clip);
        out[i].plan_clip = clip;
    }
    // the program
    static Program *program;
    if (!program) {
        ResourceLayout vert = {};
        vert.input_mask = (1u << 2) | (1u << 4);
        vert.push_constant_size = sizeof(Push);
        vert.sets[1].storage_buffer_mask = 1u;
        vert.sets[1].meta[0].array_size = 1;
        vert.sets[2].storage_buffer_mask = 7u;
        for (int b = 0; b < 3; b++) {
            vert.sets[2].meta[b].array_size = 1;
        }
        ResourceLayout frag = {};
        frag.output_mask = 1;
        program = dev.request_program(
            dev.request_shader(gsn_spv_nd_000_oracle_vert, sizeof(gsn_spv_nd_000_oracle_vert), &vert),
            dev.request_shader(gsn_spv_nd_oracle_frag, sizeof(gsn_spv_nd_oracle_frag), &frag));
    }
    // the buffers: the vertices (the arena), the records, the Picture (nothing moves), the results
    uint32_t corners = (vertex_count + 2) / 3 * 3;
    auto buffer = [&](VkDeviceSize size, const void *data, BufferDomain domain) {
        BufferCreateInfo info = {};
        info.domain = domain;
        info.size = std::max<VkDeviceSize>(size, 256);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        std::vector<uint8_t> zeros;
        if (!data) {
            zeros.resize(info.size);
            data = zeros.data();
        }
        return dev.create_buffer(info, data);
    };
    BufferHandle mesh = buffer(VkDeviceSize(vertex_count) * ND_VERTEX_BYTES, vertices, BufferDomain::Device);
    BufferHandle records = buffer(VkDeviceSize(draw_count) * sizeof(NdConst), consts.data(), BufferDomain::Device);
    BufferHandle picture = buffer(256, nullptr, BufferDomain::Device);
    BufferHandle results = buffer(VkDeviceSize(corners) * 32, nullptr, BufferDomain::CachedHost);
    ImageHandle target = dev.create_image(ImageCreateInfo::render_target(1, 1, VK_FORMAT_R8G8B8A8_UNORM), nullptr);

    CommandBufferHandle c = dev.request_command_buffer();
    RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = &target->get_view();
    rp.clear_attachments = 1;
    c->begin_render_pass(rp);
    c->set_opaque_state();
    c->set_program(program);
    c->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    c->set_cull_mode(VK_CULL_MODE_NONE);
    c->set_depth_test(false, false);
    c->set_storage_buffer(1, 0, *picture);
    c->set_storage_buffer(2, 0, *mesh);
    c->set_storage_buffer(2, 1, *records);
    c->set_storage_buffer(2, 2, *results);
    Push push = {};
    push.inv_size[0] = push.inv_size[1] = 1.0f / 4096.0f;
    push.zmax = 0xFFFFFFFFu;
    c->push_constants(&push, 0, sizeof(push));
    // a corner a vertex: its arena number, gouraud (its own colour), its run's record
    struct Corner {
        uint32_t rgba;
        float vertex;
    };
    auto *cv = static_cast<Corner *>(c->allocate_vertex_data(0, VkDeviceSize(corners) * sizeof(Corner), sizeof(Corner)));
    for (uint32_t i = 0; i < corners; i++) {
        uint32_t v = i < vertex_count ? i : 0;
        cv[i].rgba = ND_CORNER_GOURAUD | vertex_draw[v] << ND_CORNER_DRAW_SHIFT;
        cv[i].vertex = float(v);
    }
    c->set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Corner, rgba));
    c->set_vertex_attrib(4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Corner, vertex));
    c->draw(corners);
    c->end_render_pass();
    c->barrier(VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
               VK_ACCESS_HOST_READ_BIT);
    Fence fence;
    dev.submit(c, &fence);
    fence->wait();
    const uint32_t *r = static_cast<const uint32_t *>(dev.map_host_buffer(*results, MEMORY_ACCESS_READ_BIT));
    for (uint32_t i = 0; i < vertex_count; i++) {
        memcpy(out[i].gpu, r + size_t(i) * 8, 32);
    }
    dev.unmap_host_buffer(*results, MEMORY_ACCESS_READ_BIT);
    return 1;
}

namespace {
