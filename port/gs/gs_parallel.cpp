// The GS on the PC: paraLLEl-GS (third_party/parallel-gs), the GS rasteriser in Vulkan compute, behind
// the small C interface port/platform/gs.c calls (port/include/port/platform.h, "the GS backend").
// All of it runs on the DMA controller's thread, which is the only one that talks to the GS.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "context.hpp"
#include "device.hpp"
#include "gs_interface.hpp"

extern "C" {
#include "port/platform.h"
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
    info.force_progressive = true;
    info.anti_blur = true;
    info.adapt_to_internal_horizontal_resolution = true;

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
