// The GS on the PC: paraLLEl-GS (third_party/parallel-gs), the GS rasteriser in Vulkan compute, behind
// the small C interface port/platform/gs.c calls (port/include/port/platform.h, "the GS backend").
// All of it runs on the DMA controller's thread, which is the only one that talks to the GS.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "context.hpp"
#include "device.hpp"
#include "gs_interface.hpp"

extern "C" {
#include "port/platform.h"
#include "port/savestate.h"
}

using namespace ParallelGS;
using namespace Vulkan;

namespace {
Context *context;
Device *device;
GSInterface *gs;
BufferHandle readback;
VkDeviceSize readback_size;
}

extern "C" int port_gs_backend_open(void) {
    if (!Context::init_loader(nullptr)) {
        fprintf(stderr, "[gs] no Vulkan loader\n");
        return 0;
    }
    context = new Context;
    context->set_num_thread_indices(1);
    if (!context->init_instance_and_device(nullptr, 0, nullptr, 0,
                                           CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
                                               CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
                                               CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT)) {
        fprintf(stderr, "[gs] no Vulkan device that paraLLEl-GS can use\n");
        delete context;
        context = nullptr;
        return 0;
    }
    device = new Device;
    device->set_context(*context);
    device->init_frame_contexts(4);

    gs = new GSInterface;
    GSOptions options = {};
    if (!gs->init(device, options)) {
        fprintf(stderr, "[gs] paraLLEl-GS did not start\n");
        return 0;
    }
    // the same packets give the same pictures
    DebugMode debug = {};
    debug.deterministic_timeline_query = true;
    gs->set_debug_mode(debug);
    fprintf(stderr, "[gs] paraLLEl-GS on %s\n", device->get_gpu_properties().deviceName);
    return 1;
}

extern "C" void port_gs_backend_gif(int path, const void *data, size_t bytes) {
    gs->gif_transfer(uint32_t(path), data, bytes);
}

extern "C" void port_gs_backend_read_fifo(void *dst, uint32_t qwords) {
    gs->read_transfer_fifo(dst, qwords);
}

extern "C" int port_gs_backend_vsync(const uint64_t *privileged, int field, int want_pixels,
                                     PortGsFrame *frame) {
    auto &priv = gs->get_priv_register_state();
    // PMODE..BGCOLOR, each 64-bit register at a 16-byte stride, as in the register page
    memcpy(priv.qwords_lo, privileged, 0xF0);

    VSyncInfo info = {};
    info.phase = uint32_t(field);
    info.dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
    info.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    // the PC's ways from the GS's raw output, each of which can be turned off to compare (main_host.c)
    info.force_progressive = port_options.progressive != 0;
    info.anti_blur = port_options.anti_blur != 0;
    info.adapt_to_internal_horizontal_resolution = port_options.adapt_width != 0;
    info.skip_deinterlace = !port_options.deinterlace;

    gs->flush();
    ScanoutResult result = gs->vsync(info);
    if (!want_pixels || !result.image) {
        return 0;
    }

    uint32_t width = result.image->get_width();
    uint32_t height = result.image->get_height();
    if (width * height > PORT_GS_FRAME_PIXELS) {
        return 0;
    }
    VkDeviceSize size = VkDeviceSize(width) * height * 4;
    if (!readback || readback_size < size) {
        BufferCreateInfo buffer_info = {};
        buffer_info.domain = BufferDomain::CachedHost;
        buffer_info.size = size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        readback = device->create_buffer(buffer_info, nullptr);
        readback_size = size;
    }

    auto cmd = device->request_command_buffer();
    cmd->copy_image_to_buffer(*readback, *result.image, 0, {0, 0, 0}, {width, height, 1}, 0, 0,
                              {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                 VK_ACCESS_HOST_READ_BIT);
    Fence fence;
    device->submit(cmd, &fence);
    fence->wait();

    auto *pixels = static_cast<const uint8_t *>(device->map_host_buffer(*readback, MEMORY_ACCESS_READ_BIT));
    bool bgra = result.image->get_format() == VK_FORMAT_B8G8R8A8_UNORM;
    for (uint32_t i = 0; i < width * height; i++) {
        const uint8_t *p = pixels + i * 4;
        uint8_t r = bgra ? p[2] : p[0], b = bgra ? p[0] : p[2];
        // RGBA in memory order; the scanout's alpha means nothing on a screen
        frame->rgba[i] = uint32_t(r) | (uint32_t(p[1]) << 8) | (uint32_t(b) << 16) | 0xFF000000u;
    }
    device->unmap_host_buffer(*readback, MEMORY_ACCESS_READ_BIT);
    frame->width = width;
    frame->height = height;
    return 1;
}

// ---- Save states (port/platform/gs.c) -------------------------------------------------------------
// VRAM and the registers, taken between GIF packets, so no path is inside one and no transfer waits
// for its pixels. A load starts from the GS's reset state (paths, vertex queue), then puts back VRAM
// and the registers and has paraLLEl-GS take them all as new.
//
// And what the scanout keeps from the last pictures, without which the first four after a load would
// differ from the run that saved: the last four fields, which the deinterlacer weaves each picture
// from (GSRenderer::vsync_last_fields), and the countdown that turns --no-progressive's way on for four
// vblanks after a complex SCANMSK (has_complex_scanmsk_timeout). Both are private: the functions
// below reach them through pointers to members named in an explicit template instantiation, where
// C++ lets private names be used.
static const size_t vram_bytes = 4 * 1024 * 1024; // GSOptions' vram_size, the GS's 4 MB
static const int history_fields = 4;

ImageHandle *gs_history(GSInterface &g);
uint32_t &gs_scanmsk_timeout(GSInterface &g);

template <typename RendererMember, RendererMember renderer_member, typename FieldsMember, FieldsMember fields_member,
          typename TrackerMember, TrackerMember tracker_member>
struct GSPrivate {
    friend ImageHandle *gs_history(GSInterface &g) {
        return (g.*renderer_member).*fields_member;
    }
    friend uint32_t &gs_scanmsk_timeout(GSInterface &g) {
        return (g.*tracker_member).has_complex_scanmsk_timeout;
    }
};
template struct GSPrivate<decltype(&GSInterface::renderer), &GSInterface::renderer,
                          decltype(&GSRenderer::vsync_last_fields), &GSRenderer::vsync_last_fields,
                          decltype(&GSInterface::state_tracker), &GSInterface::state_tracker>;

static ImageHandle *history(void) {
    return gs_history(*gs);
}

// A field: the one before it that is the same image (or -1), its size, then (when new) its pixels
struct FieldHeader {
    int32_t same_as;
    uint32_t width;
    uint32_t height;
    uint32_t present;
};

static FieldHeader field_header(const ImageHandle *fields, int i) {
    FieldHeader h = {-1, 0, 0, 0};

    if (!fields[i]) {
        return h;
    }
    h.present = 1;
    for (int j = 0; j < i; j++) {
        if (fields[j] == fields[i]) {
            h.same_as = j;
        }
    }
    h.width = fields[i]->get_width();
    h.height = fields[i]->get_height();
    return h;
}

static void save_field(PortStateIo *io, const Image &image) {
    VkDeviceSize size = VkDeviceSize(image.get_width()) * image.get_height() * 4;
    BufferCreateInfo buffer_info = {};
    buffer_info.domain = BufferDomain::CachedHost;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    BufferHandle buffer = device->create_buffer(buffer_info, nullptr);

    auto cmd = device->request_command_buffer();
    cmd->image_barrier(image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT);
    cmd->copy_image_to_buffer(*buffer, image, 0, {0, 0, 0}, {image.get_width(), image.get_height(), 1}, 0, 0,
                              {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    cmd->image_barrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                 VK_ACCESS_HOST_READ_BIT);
    Fence fence;
    device->submit(cmd, &fence);
    fence->wait();
    void *pixels = device->map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
    port_state_bytes(io, pixels, size_t(size));
    device->unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
}

static ImageHandle load_field(PortStateIo *io, uint32_t width, uint32_t height) {
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    port_state_bytes(io, pixels.data(), pixels.size());

    auto image_info = ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_R8G8B8A8_UNORM);
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.misc |= IMAGE_MISC_MUTABLE_SRGB_BIT;
    ImageInitialData initial = {pixels.data(), 0, 0};
    return device->create_image(image_info, &initial);
}

extern "C" size_t port_gs_backend_state_size(void) {
    const ImageHandle *fields = history();
    size_t size = vram_bytes + sizeof(RegisterState) + sizeof(PrivRegisterState) + sizeof(uint32_t);

    for (int i = 0; i < history_fields; i++) {
        FieldHeader h = field_header(fields, i);

        size += sizeof(h);
        if (h.present && h.same_as < 0) {
            size += size_t(h.width) * h.height * 4;
        }
    }
    return size;
}

extern "C" void port_gs_backend_state(PortStateIo *io) {
    ImageHandle *fields = history();

    if (!io->loading) {
        gs->flush();
        port_state_bytes(io, const_cast<void *>(gs->map_vram_read(0, vram_bytes)), vram_bytes);
        RegisterState registers = gs->get_register_state();
        PrivRegisterState priv = gs->get_priv_register_state();
        port_state_bytes(io, &registers, sizeof(registers));
        port_state_bytes(io, &priv, sizeof(priv));
        port_state_bytes(io, &gs_scanmsk_timeout(*gs), sizeof(uint32_t));
        for (int i = 0; i < history_fields; i++) {
            FieldHeader h = field_header(fields, i);

            port_state_bytes(io, &h, sizeof(h));
            if (h.present && h.same_as < 0) {
                save_field(io, *fields[i]);
            }
        }
        return;
    }
    gs->reset_context_state();
    port_state_bytes(io, gs->map_vram_write(0, vram_bytes), vram_bytes);
    gs->end_vram_write(0, vram_bytes);
    port_state_bytes(io, &gs->get_register_state(), sizeof(RegisterState));
    port_state_bytes(io, &gs->get_priv_register_state(), sizeof(PrivRegisterState));
    gs->clobber_register_state();
    gs->flush();
    port_state_bytes(io, &gs_scanmsk_timeout(*gs), sizeof(uint32_t));
    for (int i = 0; i < history_fields; i++) {
        FieldHeader h;

        port_state_bytes(io, &h, sizeof(h));
        if (!h.present) {
            fields[i].reset();
        } else if (h.same_as >= 0 && h.same_as < i) {
            fields[i] = fields[h.same_as];
        } else {
            fields[i] = load_field(io, h.width, h.height);
        }
    }
}
