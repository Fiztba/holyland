// ---- The temporal upscaler's calls (--upscaler fsr3, docs/port/UPSCALING.md) ------------------------------------
// Included by gsn.cpp before gsn_backend.inc, inside its namespace. The composite (gsn_composite.inc's
// sa_upscale_held) sends each picture to upscale as one call in the stream (OP_UPSCALE, Cmd::upscale); the back end's
// thread makes it here, through upscale_ffx.cpp, into its command buffer. FSR records its own compute work there past
// Granite (its pipelines, descriptor sets and barriers): the composite begins a render pass right after, and Granite
// forgets what it had bound at a render pass's beginning, so nothing it keeps is wrong after. One context for the
// stream of pictures in the order they are shown (a game's two buffers alike), made again when a size changes; the
// one before is let go once the GPU is done with it (eight of Granite's frames on: each waits for the GPU's work of
// four vblanks before).

struct UpscaleBackend {
    void *context = nullptr;
    uint32_t size[4] = {}; // the context's: the scene's width and height, the output's
    struct Retired {
        void *context;
        int frames;
    };
    std::vector<Retired> retired;
} upscale_backend;

// (OP_NEXT_FRAME) The contexts let go, freed once the GPU is done with them
void upscale_next_frame() {
    std::vector<UpscaleBackend::Retired> &r = upscale_backend.retired;
    for (size_t i = 0; i < r.size();) {
        if (--r[i].frames <= 0) {
            ffx_upscale_free(r[i].context);
            r.erase(r.begin() + ptrdiff_t(i));
        } else {
            i++;
        }
    }
}

UpscaleImage upscale_image(const Image &image) {
    return {image.get_image(), image.get_format(), image.get_width(), image.get_height()};
}

// (OP_UPSCALE) A picture upscaled; where FSR cannot, bilinear (a blit), and the composite told so
void upscale_call(CommandBuffer &c, const ArgUpscale &a) {
    UpscaleBackend &u = upscale_backend;
    uint32_t size[4] = {a.color->get_width(), a.color->get_height(), a.out->get_width(), a.out->get_height()};
    bool reset = a.reset != 0;
    if (!upscale_failed.load(std::memory_order_relaxed) && (!u.context || memcmp(u.size, size, sizeof(size)) != 0)) {
        if (u.context) {
            u.retired.push_back({u.context, 8});
        }
        auto proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            Context::get_instance_proc_addr()(gs->context->get_instance(), "vkGetDeviceProcAddr"));
        u.context = ffx_upscale_make(gs->context->get_device(), gs->context->get_gpu(), proc, size[0], size[1], size[2],
                                     size[3]);
        memcpy(u.size, size, sizeof(size));
        reset = true;
    }
    if (u.context) {
        UpscaleFrame f = {};
        f.cmd = c.get_command_buffer();
        f.color = upscale_image(*a.color);
        f.depth = upscale_image(*a.depth);
        f.motion = upscale_image(*a.motion);
        f.output = upscale_image(*a.out);
        f.render_w = size[0];
        f.render_h = size[1];
        f.jitter[0] = a.jitter[0];
        f.jitter[1] = a.jitter[1];
        f.frame_ms = a.frame_ms;
        f.near_plane = a.near_plane;
        f.fov_y = a.fov_y;
        f.reset = reset;
        if (ffx_upscale_dispatch(u.context, f)) {
            return;
        }
    }
    upscale_failed.store(true, std::memory_order_relaxed);
    c.blit_image(*a.out, *a.color, {0, 0, 0}, {int32_t(size[2]), int32_t(size[3]), 1}, {0, 0, 0},
                 {int32_t(size[0]), int32_t(size[1]), 1}, 0, 0, 0, 0, 1, VK_FILTER_LINEAR);
}
