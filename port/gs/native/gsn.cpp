// The native GS (docs/port/STEP3.md, port/gs/native/gsn.h): the GS drawn with the GPU's rasteriser.
//
// What the GS is asked for comes as GIF packets. They are read here as the GS reads them: the
// registers, the vertex queue and its primitives, the transfers into and out of VRAM, the CLUT. A run
// of primitives with no register written between them is a batch; each batch is drawn with Vulkan as
// soon as the next register write ends it.
//
// VRAM is kept twice:
// - on the CPU (gsn_vram.c), in the GS's own layout: what IMAGE uploads write, what local-to-local
//   copies move, what textures are decoded from;
// - on the GPU as render targets: an image for each frame buffer and Z buffer the game draws into,
//   keyed by its base pointer, width and format. A target can be newer than the CPU's copy of its
//   pages ("dirty"); whatever needs those pages on the CPU (a texture in another format, a read-back,
//   an upload in another shape) has the target written back first.
// A texture that is a target (the frame read back as a texture, the scrolling texture targets, the
// copy of the frame at 0x2BC0) is read from the target itself. Anything else is decoded from the CPU's
// VRAM into an integer image of the raw texels, cached by its pages' write counts, and looked up and
// filtered in the shader (port/gs/native/shaders/gs.frag), so a CLUT change needs no new texture.
//
// Blending is Vulkan's where the GS's equation maps onto it (most draws); otherwise, and for draws
// that read the target they draw into, the shader blends from a copy of the target taken just before,
// in the GS's integer arithmetic.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "context.hpp"
#include "device.hpp"

extern "C" {
#include "port/gs_capture_format.h"
#include "port/platform.h"
#include "gsn.h"
#include "gsn_vram.h"
}
#include "gsn_shaders.h"

using namespace Vulkan;

GsnStats gsn_stats;
extern "C" void port_gs_backend_share_queue(void *other_device);

namespace {

// ---- GS registers --------------------------------------------------------------------------------
enum {
    R_PRIM = 0x00, R_RGBAQ = 0x01, R_ST = 0x02, R_UV = 0x03, R_XYZF2 = 0x04, R_XYZ2 = 0x05,
    R_TEX0_1 = 0x06, R_TEX0_2 = 0x07, R_CLAMP_1 = 0x08, R_CLAMP_2 = 0x09, R_FOG = 0x0A,
    R_XYZF3 = 0x0C, R_XYZ3 = 0x0D, R_TEX1_1 = 0x14, R_TEX1_2 = 0x15, R_TEX2_1 = 0x16, R_TEX2_2 = 0x17,
    R_XYOFFSET_1 = 0x18, R_XYOFFSET_2 = 0x19, R_PRMODECONT = 0x1A, R_PRMODE = 0x1B, R_TEXCLUT = 0x1C,
    R_SCANMSK = 0x22, R_MIPTBP1_1 = 0x34, R_MIPTBP1_2 = 0x35, R_MIPTBP2_1 = 0x36, R_MIPTBP2_2 = 0x37,
    R_TEXA = 0x3B, R_FOGCOL = 0x3D, R_TEXFLUSH = 0x3F, R_SCISSOR_1 = 0x40, R_SCISSOR_2 = 0x41,
    R_ALPHA_1 = 0x42, R_ALPHA_2 = 0x43, R_DIMX = 0x44, R_DTHE = 0x45, R_COLCLAMP = 0x46,
    R_TEST_1 = 0x47, R_TEST_2 = 0x48, R_PABE = 0x49, R_FBA_1 = 0x4A, R_FBA_2 = 0x4B,
    R_FRAME_1 = 0x4C, R_FRAME_2 = 0x4D, R_ZBUF_1 = 0x4E, R_ZBUF_2 = 0x4F, R_BITBLTBUF = 0x50,
    R_TRXPOS = 0x51, R_TRXREG = 0x52, R_TRXDIR = 0x53, R_HWREG = 0x54, R_SIGNAL = 0x60,
    R_FINISH = 0x61, R_LABEL = 0x62,
};

inline uint32_t bits(uint64_t v, unsigned shift, unsigned count) {
    return uint32_t((v >> shift) & ((uint64_t(1) << count) - 1));
}

inline float as_float(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

bool psm_is_palette(uint32_t psm) {
    return psm == GSN_PSMT8 || psm == GSN_PSMT4 || psm == GSN_PSMT8H || psm == GSN_PSMT4HL || psm == GSN_PSMT4HH;
}

bool psm_is_16(uint32_t psm) {
    return psm == GSN_PSMCT16 || psm == GSN_PSMCT16S || psm == GSN_PSMZ16 || psm == GSN_PSMZ16S;
}

bool psm_is_depth(uint32_t psm) {
    return psm >= 0x30;
}

// Formats that share one layout of VRAM: a target in one can be read as another of the same
uint32_t psm_layout(uint32_t psm) {
    switch (psm) {
    case GSN_PSMCT24:
        return GSN_PSMCT32;
    case GSN_PSMZ24:
        return GSN_PSMZ32;
    default:
        return psm;
    }
}

// a page of a format: 8 KB, this many pixels across and down
uint32_t page_width(uint32_t psm) {
    return psm == GSN_PSMT8 || psm == GSN_PSMT4 ? 128 : 64;
}

uint32_t page_height(uint32_t psm) {
    if (psm == GSN_PSMT4) {
        return 128;
    }
    if (psm == GSN_PSMT8 || psm_is_16(psm)) {
        return 64;
    }
    return 32;
}

const uint32_t vram_pages = GSN_VRAM_BYTES / 8192;

// ---- Vertices -----------------------------------------------------------------------------------
// One vertex as the GS queued it
struct QVertex {
    uint32_t x, y; // 12.4, before XYOFFSET
    uint32_t z;
    uint32_t rgba;
    float s, t, q;
    uint32_t u, v; // 10.4
    uint32_t fog;
};

// One corner of a triangle as the shaders take it (shaders/gs.vert)
struct Vtx {
    float x, y;
    uint32_t z;
    uint32_t rgba;
    float s, t, q;
    float fog;
};
static_assert(sizeof(Vtx) == 32, "Vtx is the vertex shader's input");

// The shaders' push constants (shaders/gs_common.h)
struct Push {
    float inv_size[2];
    float zscale;
    uint32_t zmax;
    uint32_t flags;
    uint32_t tex_size;
    uint32_t clamp_lo;
    uint32_t clamp_hi;
    uint32_t test;
    uint32_t fogcol;
    uint32_t alpha;
    uint32_t texa;
    uint32_t tex1;
    float lod_k;
    int32_t src_offset[2];
    int32_t dst_offset[2];
    uint32_t misc;
    uint32_t fbmsk;
};
static_assert(sizeof(Push) == 80, "Push is the shaders' push constant block");

enum : uint32_t {
    F_TME = 0x1, F_FST = 0x2, F_TFX_SHIFT = 2, F_TCC = 0x10, F_FGE = 0x20, F_SHADER_BLEND = 0x40,
    F_SRC_SHIFT = 8, F_ALPHA_FACTOR = 0x1000, F_PASS_SHIFT = 13, F_DATE = 0x8000,
};
enum : uint32_t {
    SRC_CLUT = 0, SRC_CT32 = 1, SRC_CT24 = 2, SRC_CT16 = 3, SRC_TARGET_CT32 = 4, SRC_TARGET_CT24 = 5,
    SRC_TARGET_CT16 = 6,
};
enum : uint32_t { M_FBA = 0x1, M_COLCLAMP = 0x2, M_DST16 = 0x4, M_DST24 = 0x8, M_PABE = 0x10 };

// ---- Render targets and textures ----------------------------------------------------------------
struct Target {
    uint32_t bp, bw, psm; // bp in 256-byte blocks
    uint32_t width, height;
    ImageHandle image;
    bool depth;
    bool dirty; // newer than the CPU's VRAM
    uint64_t used;
};

struct Texture {
    uint32_t tbp, tbw, psm, tw, th, levels;
    uint32_t mip_tbp[7], mip_tbw[7];
    ImageHandle image;
    uint32_t page_lo[7], page_count[7];
    uint64_t decoded; // the VRAM write count when it was decoded
    uint64_t hash;    // of the bytes it was decoded from (texture_hash)
    uint64_t used;
};

// The GIF's three paths, read as gs.c reads them
struct GifPath {
    uint32_t left, flg, nreg, reg, eop, in_packet, dwords;
    uint64_t regs;
};

struct Gs {
    // ---- the GS
    uint64_t reg[256];
    uint32_t internal_q;
    uint16_t clut[512];   // the CLUT buffer on the chip: 512 halfwords
    uint32_t clut_cbp[2]; // CBP0, CBP1: the last CBP loaded by CLD 2..5
    uint64_t clut_version;
    QVertex queue[3];
    uint32_t queue_count;
    uint32_t fan_first; // a fan's first vertex is queue[0], kept
    GifPath paths[4];
    // transfers
    GsnUpload upload;
    uint32_t transfer_dir; // TRXDIR of the transfer under way (0 host to local, 1 local to host, 3 none)
    std::vector<uint8_t> readback;
    size_t readback_at;
    // the CPU's VRAM and how often each page was written
    std::vector<uint8_t> vram;
    uint64_t page_written[512];
    uint64_t vram_writes;

    // ---- Vulkan
    Context *context = nullptr;
    bool own_context = false;
    Device *device = nullptr;
    Program *program = nullptr;
    CommandBufferHandle cmd;
    ImageHandle dummy_raw, dummy_rgba, snapshot, scanout;
    BufferHandle readback_buffer;
    VkDeviceSize readback_size = 0;
    std::vector<std::unique_ptr<Target>> targets;
    std::vector<std::unique_ptr<Texture>> textures;
    uint64_t tick = 0;
    // the render pass under way
    Target *pass_color = nullptr;
    Target *pass_depth = nullptr;
    bool in_pass = false;

    // ---- the batch being gathered: triangles since the last register write
    std::vector<Vtx> verts;
    uint32_t batch_prim = 0; // PRIM's attributes (PRMODE's when PRMODECONT says)

    // the last field shown (field mode: the picture is woven from the last two)
    std::vector<uint32_t> fields[2];
    uint32_t field_width = 0, field_height = 0;
};

Gs *gs;
int log_level; // KF4_GSN_LOG: 1 says when targets are made and dropped, 2 every batch too

#define GSN_LOG(level, ...)                                                                                \
    do {                                                                                                   \
        if (log_level >= (level)) {                                                                        \
            fprintf(stderr, "[gsn] " __VA_ARGS__);                                                         \
        }                                                                                                  \
    } while (0)

// ---- Vulkan helpers ----------------------------------------------------------------------------
CommandBuffer &cmd() {
    if (!gs->cmd) {
        gs->cmd = gs->device->request_command_buffer();
    }
    return *gs->cmd;
}

void end_pass() {
    if (gs->in_pass) {
        gs->cmd->end_render_pass();
        gs->in_pass = false;
        gs->pass_color = nullptr;
        gs->pass_depth = nullptr;
        gs->cmd->barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
}

// Everything recorded so far, sent; with wait, finished too
void submit(bool wait) {
    end_pass();
    if (!gs->cmd) {
        return;
    }
    if (wait) {
        Fence fence;
        gs->device->submit(gs->cmd, &fence);
        fence->wait();
    } else {
        gs->device->submit(gs->cmd);
    }
    gs->cmd.reset();
}

// A barrier between transfers and the draws or transfers after them
void transfer_barrier() {
    cmd().barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

ImageHandle make_image(uint32_t width, uint32_t height, VkFormat format, uint32_t levels, bool attachment) {
    ImageCreateInfo info = ImageCreateInfo::render_target(width, height, format);
    info.levels = levels;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (attachment) {
        info.usage |= format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                     : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    info.layout = ImageLayout::General;
    info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
    return gs->device->create_image(info, nullptr);
}

// Pixels from the GPU: an image's rectangle, as RGBA8 (or D32 floats), when the GPU is done
const uint8_t *read_image(const Image &image, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    VkDeviceSize size = VkDeviceSize(w) * h * 4;
    if (!gs->readback_buffer || gs->readback_size < size) {
        BufferCreateInfo info = {};
        info.domain = BufferDomain::CachedHost;
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        gs->readback_buffer = gs->device->create_buffer(info, nullptr);
        gs->readback_size = size;
    }
    end_pass();
    VkImageAspectFlags aspect = image.get_format() == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                           : VK_IMAGE_ASPECT_COLOR_BIT;
    transfer_barrier();
    cmd().copy_image_to_buffer(*gs->readback_buffer, image, 0, {int32_t(x), int32_t(y), 0}, {w, h, 1}, 0, 0,
                               {aspect, 0, 0, 1});
    cmd().barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                  VK_ACCESS_HOST_READ_BIT);
    submit(true);
    return static_cast<const uint8_t *>(gs->device->map_host_buffer(*gs->readback_buffer, MEMORY_ACCESS_READ_BIT));
}

void done_reading() {
    gs->device->unmap_host_buffer(*gs->readback_buffer, MEMORY_ACCESS_READ_BIT);
}

// ---- The CPU's VRAM ----------------------------------------------------------------------------
void pages_written(uint32_t lo, uint32_t count) {
    gs->vram_writes++;
    for (uint32_t i = 0; i < count && i < vram_pages; i++) {
        gs->page_written[(lo + i) % vram_pages] = gs->vram_writes;
    }
}

// The pages a buffer of this format, base and width covers, `height` pixels down
void buffer_pages(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t width, uint32_t height, uint32_t *lo,
                  uint32_t *count) {
    uint32_t pw = page_width(psm), ph = page_height(psm);
    uint32_t across = std::max(std::max(bw, 1u) * 64 / pw, (width + pw - 1) / pw);
    uint32_t rows = (height + ph - 1) / ph;
    *lo = bp / 32;
    *count = across * rows + (bp % 32 ? 1 : 0);
}

bool pages_overlap(uint32_t lo_a, uint32_t count_a, uint32_t lo_b, uint32_t count_b) {
    // pages wrap at 4 MB: compare on the circle
    uint32_t d = (lo_b + vram_pages - lo_a) % vram_pages;
    uint32_t e = (lo_a + vram_pages - lo_b) % vram_pages;
    return d < count_a || e < count_b;
}

void target_pages(const Target &t, uint32_t *lo, uint32_t *count) {
    buffer_pages(t.bp, t.bw, t.psm, t.width, t.height, lo, count);
}

// ---- Targets ------------------------------------------------------------------------------------
// The GS's bytes of one pixel of a colour target's image (RGBA8), for its format
uint32_t target_pixel_to_vram(uint32_t rgba, uint32_t psm) {
    if (psm == GSN_PSMCT16 || psm == GSN_PSMCT16S) {
        uint32_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = rgba >> 24;
        return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >> 7) << 15);
    }
    if (psm == GSN_PSMCT24) {
        return rgba & 0xFFFFFF;
    }
    return rgba;
}

uint32_t vram_to_target_pixel(uint32_t v, uint32_t psm) {
    if (psm == GSN_PSMCT16 || psm == GSN_PSMCT16S) {
        return ((v & 0x1F) << 3) | (((v >> 5) & 0x1F) << 11) | (((v >> 10) & 0x1F) << 19) | ((v & 0x8000) ? 0x80000000u : 0);
    }
    return v;
}

double depth_scale(uint32_t psm) {
    return psm == GSN_PSMZ32 ? 1.0 / 4294967296.0 : 1.0 / 16777216.0;
}

uint32_t depth_max(uint32_t psm) {
    return psm == GSN_PSMZ32 ? 0xFFFFFFFFu : psm == GSN_PSMZ24 ? 0xFFFFFFu : 0xFFFFu;
}

// The target's pixels into the CPU's VRAM
void write_back(Target &t) {
    if (!t.dirty) {
        return;
    }
    gsn_stats.writebacks++;
    GSN_LOG(1, "write back target bp %#x bw %u psm %#x %ux%u\n", t.bp, t.bw, t.psm, t.width, t.height);
    const uint8_t *pixels = read_image(*t.image, 0, 0, t.width, t.height);
    for (uint32_t y = 0; y < t.height; y++) {
        for (uint32_t x = 0; x < t.width; x++) {
            uint32_t v;
            memcpy(&v, pixels + (size_t(y) * t.width + x) * 4, 4);
            if (t.depth) {
                float f;
                memcpy(&f, &v, 4);
                double z = double(f) / depth_scale(t.psm) + 0.5;
                v = z >= double(depth_max(t.psm)) ? depth_max(t.psm) : uint32_t(z);
            } else {
                v = target_pixel_to_vram(v, t.psm);
            }
            gsn_vram_write(gs->vram.data(), t.psm, t.bp, t.bw, x, y, v);
        }
    }
    done_reading();
    uint32_t lo, count;
    target_pages(t, &lo, &count);
    pages_written(lo, count);
    t.dirty = false;
}

// The CPU's VRAM into a rectangle of the target
void load_target_rect(Target &t, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    if (x0 >= t.width || y0 >= t.height) {
        return;
    }
    w = std::min(w, t.width - x0);
    h = std::min(h, t.height - y0);
    if (w == 0 || h == 0) {
        return;
    }
    end_pass();
    transfer_barrier();
    VkImageAspectFlags aspect = t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t *out = static_cast<uint32_t *>(cmd().update_image(*t.image, {int32_t(x0), int32_t(y0), 0}, {w, h, 1}, w, h,
                                                               {aspect, 0, 0, 1}));
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = gsn_vram_read(gs->vram.data(), t.psm, t.bp, t.bw, x0 + x, y0 + y);
            if (t.depth) {
                float f = float(double(v) * depth_scale(t.psm));
                memcpy(&v, &f, 4);
            } else {
                v = vram_to_target_pixel(v, t.psm);
            }
            out[size_t(y) * w + x] = v;
        }
    }
    transfer_barrier();
}

void drop_target(Target *t) {
    GSN_LOG(1, "drop target bp %#x bw %u psm %#x %ux%u%s\n", t->bp, t->bw, t->psm, t->width, t->height,
            t->dirty ? " (dirty)" : "");
    if (gs->pass_color == t || gs->pass_depth == t) {
        end_pass();
    }
    for (size_t i = 0; i < gs->targets.size(); i++) {
        if (gs->targets[i].get() == t) {
            gs->targets.erase(gs->targets.begin() + long(i));
            return;
        }
    }
}

// Everything on the GPU that is newer than these pages of the CPU's VRAM, written back; and the
// targets over them that are not `keep`, gone (they are about to be written in another shape)
void claim_pages(uint32_t lo, uint32_t count, const Target *keep, bool drop) {
    for (size_t i = 0; i < gs->targets.size();) {
        Target *t = gs->targets[i].get();
        uint32_t tlo, tcount;
        target_pages(*t, &tlo, &tcount);
        if (t != keep && pages_overlap(lo, count, tlo, tcount)) {
            write_back(*t);
            if (drop) {
                drop_target(t);
                continue;
            }
        }
        i++;
    }
}

Target *find_target(uint32_t bp, uint32_t bw, uint32_t psm) {
    for (auto &t : gs->targets) {
        if (t->bp == bp && t->bw == bw && psm_layout(t->psm) == psm_layout(psm) && t->depth == psm_is_depth(psm)) {
            return t.get();
        }
    }
    return nullptr;
}

// The target that is the buffer at bp, at least width x height, made (from the CPU's VRAM) or grown
Target *get_target(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t width, uint32_t height) {
    Target *t = find_target(bp, bw, psm);
    width = std::max(width, std::max(bw, 1u) * 64);
    if (t && t->width >= width && t->height >= height) {
        t->psm = psm;
        t->used = gs->tick;
        return t;
    }
    VkFormat format = psm_is_depth(psm) ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
    if (t) {
        // grown: the old picture kept, the new part from VRAM
        uint32_t new_w = std::max(width, t->width), new_h = std::max(height, t->height);
        ImageHandle image = make_image(new_w, new_h, format, 1, true);
        end_pass();
        Target old = *t;
        t->image = image;
        t->width = new_w;
        t->height = new_h;
        // what the old target did not cover comes from VRAM: the whole of it first, then the old on top
        uint32_t lo, count;
        target_pages(*t, &lo, &count);
        claim_pages(lo, count, t, true);
        load_target_rect(*t, 0, 0, new_w, new_h);
        VkImageAspectFlags aspect = t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        cmd().copy_image(*t->image, *old.image, {0, 0, 0}, {0, 0, 0}, {old.width, old.height, 1},
                         {aspect, 0, 0, 1}, {aspect, 0, 0, 1});
        transfer_barrier();
        t->used = gs->tick;
        return t;
    }
    std::unique_ptr<Target> made(new Target());
    made->bp = bp;
    made->bw = bw;
    made->psm = psm;
    made->width = width;
    made->height = height;
    made->depth = psm_is_depth(psm);
    made->dirty = false;
    made->used = gs->tick;
    made->image = make_image(width, height, format, 1, true);
    GSN_LOG(1, "new target bp %#x bw %u psm %#x %ux%u\n", bp, bw, psm, width, height);
    uint32_t lo, count;
    target_pages(*made, &lo, &count);
    // whatever else was drawn over these pages goes back to VRAM first, and stops being a target
    claim_pages(lo, count, nullptr, true);
    load_target_rect(*made, 0, 0, width, height);
    gs->targets.push_back(std::move(made));
    return gs->targets.back().get();
}

// ---- Textures ----------------------------------------------------------------------------------
VkFormat raw_format(uint32_t psm) {
    if (psm_is_palette(psm)) {
        return VK_FORMAT_R8_UINT;
    }
    if (psm_is_16(psm)) {
        return VK_FORMAT_R16_UINT;
    }
    return VK_FORMAT_R32_UINT;
}

bool texture_current(const Texture &tex) {
    for (uint32_t l = 0; l < tex.levels; l++) {
        for (uint32_t i = 0; i < tex.page_count[l]; i++) {
            if (gs->page_written[(tex.page_lo[l] + i) % vram_pages] > tex.decoded) {
                return false;
            }
        }
    }
    return true;
}

// The bytes of a texture's pages: textures that take turns in the same VRAM (much of what the game
// uploads each frame) are decoded once each and found again by this
uint64_t texture_hash(const Texture &tex) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t(tex.tbp) << 32 | tex.tbw << 24 | tex.psm << 16 | tex.tw << 8 | tex.th);
    for (uint32_t l = 0; l < tex.levels; l++) {
        h ^= uint64_t(tex.mip_tbp[l]) << 20 | tex.mip_tbw[l];
        for (uint32_t i = 0; i < tex.page_count[l]; i++) {
            const uint64_t *p = reinterpret_cast<const uint64_t *>(gs->vram.data()) + size_t((tex.page_lo[l] + i) % vram_pages) * 1024;
            for (int k = 0; k < 1024; k += 4) {
                h = (h ^ p[k]) * 0x100000001B3ull + (p[k + 1] ^ (p[k + 2] << 1) ^ (p[k + 3] << 2));
                h ^= h >> 29;
            }
        }
    }
    return h;
}

// Decoded textures by the hash of what they were decoded from
std::unordered_map<uint64_t, ImageHandle> decoded_pool;
std::deque<uint64_t> decoded_order;

void decode_texture(Texture &tex) {
    VkFormat format = raw_format(tex.psm);
    uint64_t hash = texture_hash(tex);
    tex.decoded = gs->vram_writes;
    if (tex.image && hash == tex.hash) {
        return; // pages written, but not the bytes this texture has
    }
    tex.hash = hash;
    auto found = decoded_pool.find(hash);
    if (found != decoded_pool.end()) {
        tex.image = found->second;
        gsn_stats.texture_reuses++;
        return;
    }
    tex.image = make_image(1u << tex.tw, 1u << tex.th, format, tex.levels, false);
    decoded_pool[hash] = tex.image;
    decoded_order.push_back(hash);
    if (decoded_order.size() > 2048) {
        decoded_pool.erase(decoded_order.front());
        decoded_order.pop_front();
    }
    end_pass();
    transfer_barrier();
    for (uint32_t l = 0; l < tex.levels; l++) {
        uint32_t w = std::max(1u, (1u << tex.tw) >> l), h = std::max(1u, (1u << tex.th) >> l);
        void *out = cmd().update_image(*tex.image, {0, 0, 0}, {w, h, 1}, w, h, {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1});
        uint32_t bp = tex.mip_tbp[l], bw = tex.mip_tbw[l];
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint32_t v = gsn_vram_read(gs->vram.data(), tex.psm, bp, bw, x, y);
                size_t i = size_t(y) * w + x;
                if (format == VK_FORMAT_R8_UINT) {
                    static_cast<uint8_t *>(out)[i] = uint8_t(v);
                } else if (format == VK_FORMAT_R16_UINT) {
                    static_cast<uint16_t *>(out)[i] = uint16_t(v);
                } else {
                    static_cast<uint32_t *>(out)[i] = v;
                }
            }
        }
        gsn_stats.texture_bytes += size_t(w) * h;
    }
    transfer_barrier();
    gsn_stats.texture_decodes++;
}

Texture *get_texture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, uint32_t levels,
                     const uint32_t *mip_tbp, const uint32_t *mip_tbw) {
    for (auto &t : gs->textures) {
        if (t->tbp == tbp && t->tbw == tbw && t->psm == psm && t->tw == tw && t->th == th && t->levels == levels &&
            memcmp(t->mip_tbp, mip_tbp, levels * 4) == 0 && memcmp(t->mip_tbw, mip_tbw, levels * 4) == 0) {
            t->used = gs->tick;
            if (!texture_current(*t)) {
                decode_texture(*t);
            }
            return t.get();
        }
    }
    std::unique_ptr<Texture> tex(new Texture());
    tex->tbp = tbp;
    tex->tbw = tbw;
    tex->psm = psm;
    tex->tw = tw;
    tex->th = th;
    tex->levels = levels;
    memcpy(tex->mip_tbp, mip_tbp, levels * 4);
    memcpy(tex->mip_tbw, mip_tbw, levels * 4);
    for (uint32_t l = 0; l < levels; l++) {
        buffer_pages(mip_tbp[l], mip_tbw[l], psm, std::max(1u, (1u << tw) >> l), std::max(1u, (1u << th) >> l),
                     &tex->page_lo[l], &tex->page_count[l]);
    }
    tex->used = gs->tick;
    decode_texture(*tex);
    gs->textures.push_back(std::move(tex));
    // a few hundred textures at most are in use at once: drop the oldest beyond that
    if (gs->textures.size() > 1024) {
        auto oldest = std::min_element(gs->textures.begin(), gs->textures.end(),
                                       [](const std::unique_ptr<Texture> &a, const std::unique_ptr<Texture> &b) {
                                           return a->used < b->used;
                                       });
        if (oldest->get() != gs->textures.back().get()) {
            gs->textures.erase(oldest);
        }
    }
    return gs->textures.back().get();
}

// ---- The CLUT ----------------------------------------------------------------------------------
// TEX0's CLD: whether the CLUT buffer is loaded now, and from where
void clut_load(uint64_t tex0) {
    uint32_t psm = bits(tex0, 20, 6);
    uint32_t cbp = bits(tex0, 37, 14);
    uint32_t cpsm = bits(tex0, 51, 4);
    uint32_t csm = bits(tex0, 55, 1);
    uint32_t csa = bits(tex0, 56, 5);
    uint32_t cld = bits(tex0, 61, 3);

    if (!psm_is_palette(psm)) {
        return;
    }
    switch (cld) {
    case 0:
        return;
    case 1:
        break;
    case 2:
        gs->clut_cbp[0] = cbp;
        break;
    case 3:
        gs->clut_cbp[1] = cbp;
        break;
    case 4:
        if (gs->clut_cbp[0] == cbp) {
            return;
        }
        gs->clut_cbp[0] = cbp;
        break;
    case 5:
        if (gs->clut_cbp[1] == cbp) {
            return;
        }
        gs->clut_cbp[1] = cbp;
        break;
    default:
        return;
    }

    // the CLUT's pages may be a target's (drawn into): VRAM first
    uint32_t lo, count;
    buffer_pages(cbp, 1, cpsm == 0 ? GSN_PSMCT32 : GSN_PSMCT16, 16, 16, &lo, &count);
    claim_pages(lo, count, nullptr, false);

    uint16_t loaded[512];
    memcpy(loaded, gs->clut, sizeof(loaded));
    bool eight = psm == GSN_PSMT8 || psm == GSN_PSMT8H;
    uint32_t entries = eight ? 256 : 16;
    const uint8_t *vram = gs->vram.data();
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t x, y;
        if (csm == 0) {
            // CSM1: the entries as a 16 x 16 (or 8 x 2) rectangle, entries 8-15 and 16-23 of each 32 swapped
            uint32_t e = eight ? ((i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)) : i;
            x = eight ? e % 16 : e % 8;
            y = eight ? e / 16 : e / 8;
        } else {
            // CSM2: a line of TEXCLUT's buffer
            uint64_t texclut = gs->reg[R_TEXCLUT];
            x = bits(texclut, 6, 6) * 16 + i;
            y = bits(texclut, 12, 10);
        }
        if (cpsm == 0) { // PSMCT32
            uint32_t v = gsn_vram_read(vram, GSN_PSMCT32, cbp, csm == 0 ? 1 : bits(gs->reg[R_TEXCLUT], 0, 6), x, y);
            uint32_t at = eight ? i : (csa * 16 + i) & 0xFF;
            loaded[at] = uint16_t(v);
            loaded[256 + at] = uint16_t(v >> 16);
        } else { // PSMCT16, PSMCT16S
            uint32_t v = gsn_vram_read(vram, cpsm == 2 ? GSN_PSMCT16 : GSN_PSMCT16S, cbp,
                                       csm == 0 ? 1 : bits(gs->reg[R_TEXCLUT], 0, 6), x, y);
            uint32_t at = eight ? i : (csa * 16 + i) & 0x1FF;
            loaded[at] = uint16_t(v);
        }
    }
    if (memcmp(loaded, gs->clut, sizeof(loaded)) != 0) {
        memcpy(gs->clut, loaded, sizeof(loaded));
        gs->clut_version++;
    }
}

// The 256 colours (or 16) a palette texture's indices look up, as RGBA, for the CLUT's format
void clut_colours(uint64_t tex0, uint32_t *out) {
    uint32_t psm = bits(tex0, 20, 6);
    uint32_t cpsm = bits(tex0, 51, 4);
    uint32_t csa = bits(tex0, 56, 5);
    bool eight = psm == GSN_PSMT8 || psm == GSN_PSMT8H;
    uint32_t count = eight ? 256 : 16;
    uint64_t texa = gs->reg[R_TEXA];

    for (uint32_t i = 0; i < count; i++) {
        if (cpsm == 0) {
            uint32_t at = eight ? i : ((csa & 15) * 16 + i) & 0xFF;
            out[i] = uint32_t(gs->clut[at]) | (uint32_t(gs->clut[256 + at]) << 16);
        } else {
            uint32_t at = eight ? i : (csa * 16 + i) & 0x1FF;
            uint32_t v = gs->clut[at];
            uint32_t r = (v & 0x1F) << 3, g = ((v >> 5) & 0x1F) << 3, b = ((v >> 10) & 0x1F) << 3;
            uint32_t a = (v & 0x8000) ? bits(texa, 32, 8)
                         : (bits(texa, 15, 1) && (v & 0x7FFF) == 0) ? 0
                                                                    : bits(texa, 0, 8);
            out[i] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
    for (uint32_t i = count; i < 256; i++) {
        out[i] = 0;
    }
}

// ---- Drawing ------------------------------------------------------------------------------------
// GS blend factors and equations that Vulkan's blending does as the GS does (but for rounding)
struct FixedBlend {
    VkBlendFactor src, dst;
    VkBlendOp op;
};

bool fixed_blend(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fix, FixedBlend *out) {
    // A, B, D: 0 Cs, 1 Cd, 2 zero. C: 0 As, 1 Ad, 2 FIX
    enum { CS = 0, CD = 1, ZERO = 2 };
    VkBlendFactor f, one_minus_f;

    if (a == b || (a == 3 || b == 3 || d == 3)) {
        if (a == 3 || b == 3 || d == 3) {
            return false; // reserved
        }
        out->op = VK_BLEND_OP_ADD;
        out->src = d == CS ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        out->dst = d == CD ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        return true;
    }
    if (c == 0) {
        f = VK_BLEND_FACTOR_SRC_ALPHA;
        one_minus_f = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (c == 2 && fix <= 0x80) {
        f = VK_BLEND_FACTOR_CONSTANT_COLOR;
        one_minus_f = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    } else {
        return false; // Ad (Vulkan's is Ad / 255, the GS's Ad / 128), a FIX above 1.0
    }
    out->op = VK_BLEND_OP_ADD;
    const VkBlendFactor ONE = VK_BLEND_FACTOR_ONE, NONE = VK_BLEND_FACTOR_ZERO;
#define CASE(A, B, D, SRC, DST, OP)                                                                       \
    if (a == A && b == B && d == D) {                                                                      \
        out->src = SRC;                                                                                    \
        out->dst = DST;                                                                                    \
        out->op = OP;                                                                                      \
        return true;                                                                                       \
    }
    CASE(CS, CD, CD, f, one_minus_f, VK_BLEND_OP_ADD)
    CASE(CS, CD, ZERO, f, f, VK_BLEND_OP_SUBTRACT)
    CASE(CS, ZERO, CD, f, ONE, VK_BLEND_OP_ADD)
    CASE(CS, ZERO, ZERO, f, NONE, VK_BLEND_OP_ADD)
    CASE(CD, CS, CS, one_minus_f, f, VK_BLEND_OP_ADD)
    CASE(CD, CS, ZERO, f, f, VK_BLEND_OP_REVERSE_SUBTRACT)
    CASE(CD, ZERO, CS, ONE, f, VK_BLEND_OP_ADD)
    CASE(CD, ZERO, ZERO, NONE, f, VK_BLEND_OP_ADD)
    CASE(ZERO, CS, CD, f, ONE, VK_BLEND_OP_REVERSE_SUBTRACT)
    CASE(ZERO, CS, ZERO, f, NONE, VK_BLEND_OP_REVERSE_SUBTRACT)
    CASE(ZERO, CS, CS, one_minus_f, NONE, VK_BLEND_OP_ADD)
    CASE(ZERO, CD, CS, ONE, f, VK_BLEND_OP_SUBTRACT)
    CASE(ZERO, CD, ZERO, NONE, NONE, VK_BLEND_OP_ADD)
    CASE(ZERO, CD, CD, NONE, one_minus_f, VK_BLEND_OP_ADD)
#undef CASE
    return false; // (A - B) C + D with D one of A and B: a factor of 1 + C
}

void begin_pass(Target *color, Target *depth) {
    if (gs->in_pass && gs->pass_color == color && gs->pass_depth == depth) {
        return;
    }
    end_pass();
    RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = &color->image->get_view();
    rp.load_attachments = 1;
    rp.store_attachments = 1;
    if (depth) {
        rp.depth_stencil = &depth->image->get_view();
        rp.op_flags = RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
    }
    cmd().barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    cmd().begin_render_pass(rp);
    gs->in_pass = true;
    gs->pass_color = color;
    gs->pass_depth = depth;
    gsn_stats.passes++;
}

// A copy of the target, for a draw that reads what it draws over
const Image &snapshot_of(Target &t) {
    if (!gs->snapshot || gs->snapshot->get_width() < t.width || gs->snapshot->get_height() < t.height) {
        uint32_t w = std::max(t.width, gs->snapshot ? gs->snapshot->get_width() : 0u);
        uint32_t h = std::max(t.height, gs->snapshot ? gs->snapshot->get_height() : 0u);
        gs->snapshot = make_image(w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, false);
    }
    end_pass();
    transfer_barrier();
    cmd().copy_image(*gs->snapshot, *t.image, {0, 0, 0}, {0, 0, 0}, {t.width, t.height, 1},
                     {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    transfer_barrier();
    gsn_stats.target_copies++;
    return *gs->snapshot;
}

uint32_t effective_prim() {
    uint32_t prim = uint32_t(gs->reg[R_PRIM]) & 0x7FF;
    if (!(gs->reg[R_PRMODECONT] & 1)) {
        prim = (prim & 7) | (uint32_t(gs->reg[R_PRMODE]) & 0x7F8);
    }
    return prim;
}

// The batch, drawn
void flush_batch() {
    if (gs->verts.empty()) {
        return;
    }
    gs->tick++;
    uint32_t prim = gs->batch_prim;
    uint32_t ctxt = (prim >> 9) & 1;
    bool tme = (prim >> 4) & 1, fge = (prim >> 5) & 1, abe = (prim >> 6) & 1, fst = (prim >> 8) & 1;
    uint64_t frame = gs->reg[R_FRAME_1 + ctxt];
    uint64_t zbuf = gs->reg[R_ZBUF_1 + ctxt];
    uint64_t test = gs->reg[R_TEST_1 + ctxt];
    uint64_t alpha = gs->reg[R_ALPHA_1 + ctxt];
    uint64_t scissor = gs->reg[R_SCISSOR_1 + ctxt];
    uint64_t tex0 = gs->reg[R_TEX0_1 + ctxt];
    uint64_t tex1 = gs->reg[R_TEX1_1 + ctxt];
    uint64_t clamp = gs->reg[R_CLAMP_1 + ctxt];

    uint32_t fbp = bits(frame, 0, 9) * 32, fbw = bits(frame, 16, 6), fpsm = bits(frame, 24, 6);
    uint32_t fbmsk = uint32_t(frame >> 32);
    uint32_t zbp = bits(zbuf, 0, 9) * 32, zpsm = bits(zbuf, 24, 4) | 0x30;
    bool zmsk = bits(zbuf, 32, 1);
    uint32_t sx0 = bits(scissor, 0, 11), sx1 = bits(scissor, 16, 11);
    uint32_t sy0 = bits(scissor, 32, 11), sy1 = bits(scissor, 48, 11);
    bool ate = bits(test, 0, 1);
    uint32_t atst = bits(test, 1, 3), afail = bits(test, 12, 2);
    bool date = bits(test, 14, 1);
    uint32_t ztst = bits(test, 16, 1) ? bits(test, 17, 2) : 1;

    if (sx1 < sx0 || sy1 < sy0 || (fpsm != GSN_PSMCT32 && fpsm != GSN_PSMCT24 && fpsm != GSN_PSMCT16 &&
                                   fpsm != GSN_PSMCT16S)) {
        gs->verts.clear();
        return;
    }
    if (fpsm == GSN_PSMCT24) {
        fbmsk |= 0xFF000000u;
    }
    bool dst16 = fpsm == GSN_PSMCT16 || fpsm == GSN_PSMCT16S;
    if (dst16) {
        // a 16-bit buffer keeps its colours' top five bits and alpha's top bit: what FBMSK masks of those
        fbmsk = (fbmsk & 0x80F8F8F8u) | 0x7F070707u;
    }

    // ZTST NEVER draws nothing; ALWAYS without writing needs no Z buffer
    if (ztst == 0) {
        gs->verts.clear();
        return;
    }
    // A draw whose Z test always passes and that writes no Z needs no Z buffer: ZTST ALWAYS, or GEQUAL
    // with every vertex at the format's largest Z (the scrolling textures each frame, drawn with the
    // main Z buffer at their own width, which would otherwise make it a target of another shape)
    bool any_zwrite = !zmsk && (!ate || atst != 0 || afail == 2);
    bool always_passes = ztst == 1;
    if (ztst == 2) {
        uint32_t zmin = UINT32_MAX;
        for (const Vtx &v : gs->verts) {
            zmin = std::min(zmin, v.z);
        }
        always_passes = std::min(zmin, depth_max(zpsm)) == depth_max(zpsm);
    }
    bool need_depth = !(always_passes && !any_zwrite);
    Target *color = get_target(fbp, fbw, fpsm, fbw * 64, sy1 + 1);
    Target *depth = nullptr;
    if (need_depth) {
        // a Vulkan frame buffer's attachments are as big as each other. Making one may drop the other
        // (when their pages overlap, as the menus' Z buffer does the play's frame buffers), so both
        // are looked up again until they hold still.
        uint32_t w = color->width, h = color->height;
        for (int tries = 0; tries < 4; tries++) {
            depth = get_target(zbp, fbw, zpsm, w, h);
            w = std::max(w, depth->width);
            h = std::max(h, depth->height);
            color = get_target(fbp, fbw, fpsm, w, h);
            if (find_target(zbp, fbw, zpsm) == depth && color->width == depth->width && color->height == depth->height) {
                break;
            }
            depth = find_target(zbp, fbw, zpsm);
            if (!depth) {
                continue;
            }
            w = std::max(color->width, depth->width);
            h = std::max(color->height, depth->height);
        }
        if (find_target(zbp, fbw, zpsm) != depth || find_target(fbp, fbw, fpsm) != color) {
            static int told;
            if (!told++) {
                fprintf(stderr, "[gsn] FRAME %#x and ZBUF %#x overlap: drawn without Z\n", fbp, zbp);
            }
            color = get_target(fbp, fbw, fpsm, fbw * 64, sy1 + 1);
            depth = nullptr;
        }
    }

    Push push = {};
    push.inv_size[0] = 1.0f / float(color->width);
    push.inv_size[1] = 1.0f / float(color->height);
    push.zscale = float(depth_scale(zpsm));
    push.zmax = depth_max(zpsm);
    push.test = uint32_t(test);
    push.fogcol = uint32_t(gs->reg[R_FOGCOL]) & 0xFFFFFF;
    push.alpha = bits(alpha, 0, 2) | (bits(alpha, 2, 2) << 2) | (bits(alpha, 4, 2) << 4) | (bits(alpha, 6, 2) << 6) |
                 (bits(alpha, 32, 8) << 8);
    push.texa = uint32_t(gs->reg[R_TEXA] & 0x80FF) | (bits(gs->reg[R_TEXA], 32, 8) << 16);
    push.fbmsk = fbmsk;
    push.misc = (bits(gs->reg[R_FBA_1 + ctxt], 0, 1) ? M_FBA : 0) | (bits(gs->reg[R_COLCLAMP], 0, 1) ? M_COLCLAMP : 0) |
                (dst16 ? M_DST16 : 0) | (fpsm == GSN_PSMCT24 ? M_DST24 : 0) |
                (bits(gs->reg[R_PABE], 0, 1) ? M_PABE : 0);
    push.flags = (fge ? F_FGE : 0);

    // the texture
    const ImageView *raw_view = &gs->dummy_raw->get_view();
    const ImageView *target_view = &gs->dummy_rgba->get_view();
    bool reads_own_target = false;
    uint32_t clut[256];
    bool want_clut = false;
    if (tme) {
        uint32_t tbp = bits(tex0, 0, 14), tbw = bits(tex0, 14, 6), tpsm = bits(tex0, 20, 6);
        uint32_t tw = std::min(bits(tex0, 26, 4), 10u), th = std::min(bits(tex0, 30, 4), 10u);
        push.flags |= F_TME | (fst ? F_FST : 0) | (bits(tex0, 35, 2) << F_TFX_SHIFT) | (bits(tex0, 34, 1) ? F_TCC : 0);
        push.tex_size = (1u << tw) | ((1u << th) << 16);
        push.clamp_lo = uint32_t(clamp & 0xFFFFFF);
        push.clamp_hi = bits(clamp, 24, 20);
        push.tex1 = bits(tex1, 5, 1) | (bits(tex1, 6, 3) << 1) | (bits(tex1, 2, 3) << 4) | (bits(tex1, 0, 1) << 7) |
                    (bits(tex1, 19, 2) << 8);
        int32_t k = int32_t(bits(tex1, 32, 12) << 20) >> 20; // signed 7.4
        push.lod_k = float(k) / 16.0f;

        // a texture that is a target, or in one: read from the target
        Target *source = nullptr;
        for (auto &t : gs->targets) {
            if (t->depth || tbw != t->bw || tbp < t->bp) {
                continue;
            }
            uint32_t blocks = tbp - t->bp;
            uint32_t layout = psm_layout(tpsm);
            if (layout != psm_layout(t->psm) || blocks % 32 != 0) {
                continue;
            }
            uint32_t page = blocks / 32, across = std::max(t->bw, 1u);
            uint32_t x0 = (page % across) * page_width(t->psm), y0 = (page / across) * page_height(t->psm);
            if (y0 >= t->height) {
                continue;
            }
            source = t.get();
            push.src_offset[0] = int32_t(x0);
            push.src_offset[1] = int32_t(y0);
            break;
        }
        if (source) {
            uint32_t src = tpsm == GSN_PSMCT24 ? SRC_TARGET_CT24 : psm_is_16(tpsm) ? SRC_TARGET_CT16 : SRC_TARGET_CT32;
            push.flags |= src << F_SRC_SHIFT;
            if (source == color) {
                reads_own_target = true;
            } else {
                target_view = &source->image->get_view();
            }
        } else {
            uint32_t levels = 1;
            uint32_t mip_tbp[7] = {tbp}, mip_tbw[7] = {tbw};
            uint32_t mmin = bits(tex1, 6, 3), mxl = bits(tex1, 2, 3);
            if (mmin >= 2 && mmin <= 5 && mxl > 0) {
                levels = std::min(mxl, 6u) + 1;
                uint64_t m1 = gs->reg[R_MIPTBP1_1 + ctxt], m2 = gs->reg[R_MIPTBP2_1 + ctxt];
                for (uint32_t l = 1; l < levels; l++) {
                    uint64_t m = l <= 3 ? m1 : m2;
                    uint32_t at = ((l - 1) % 3) * 20;
                    mip_tbp[l] = bits(m, at, 14);
                    mip_tbw[l] = bits(m, at + 14, 6);
                }
                levels = std::min(levels, std::max(tw, th) + 1);
            }
            // the texture's pages may have been drawn into: back to VRAM first
            for (uint32_t l = 0; l < levels; l++) {
                uint32_t lo, count;
                buffer_pages(mip_tbp[l], mip_tbw[l], tpsm, std::max(1u, (1u << tw) >> l),
                             std::max(1u, (1u << th) >> l), &lo, &count);
                claim_pages(lo, count, nullptr, false);
            }
            Texture *tex = get_texture(tbp, tbw, tpsm, tw, th, levels, mip_tbp, mip_tbw);
            raw_view = &tex->image->get_view();
            uint32_t src = psm_is_palette(tpsm)  ? SRC_CLUT
                           : tpsm == GSN_PSMCT24 ? SRC_CT24
                           : psm_is_16(tpsm)     ? SRC_CT16
                                                 : SRC_CT32;
            push.flags |= src << F_SRC_SHIFT;
            if (src == SRC_CLUT) {
                clut_colours(tex0, clut);
                want_clut = true;
            }
        }
    }

    // blending: Vulkan's where it can, the shader's where it cannot or the draw reads its own target
    uint32_t ba = bits(alpha, 0, 2), bb = bits(alpha, 2, 2), bc = bits(alpha, 4, 2), bd = bits(alpha, 6, 2);
    uint32_t fix = bits(alpha, 32, 8);
    FixedBlend blend = {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD};
    bool shader_blend = false;
    bool partial_mask = false;
    for (int i = 0; i < 4; i++) {
        uint32_t m = (fbmsk >> (i * 8)) & 0xFF;
        if (m != 0 && m != 0xFF && !(dst16 && (m == 0x07 || m == 0x7F))) {
            partial_mask = true;
        }
    }
    if (date || partial_mask || (abe && (reads_own_target || dst16 || (push.misc & (M_PABE)) ||
                                         !(push.misc & M_COLCLAMP) || (bc == 0 && (push.misc & M_FBA)) ||
                                         !fixed_blend(ba, bb, bc, bd, fix, &blend)))) {
        shader_blend = true;
    }
    if (shader_blend) {
        push.flags |= F_SHADER_BLEND | (date ? F_DATE : 0);
        if (!abe) {
            push.alpha = (2 << 0) | (2 << 2) | (2 << 4) | (0 << 6); // (0 - 0) * FIX + Cs: no blending
        }
        gsn_stats.shader_blends++;
    } else if (abe && bc == 0 && !(ba == bb)) {
        push.flags |= F_ALPHA_FACTOR;
    }
    const Image *dest_copy = nullptr;
    if (shader_blend || reads_own_target) {
        dest_copy = &snapshot_of(*color);
        if (reads_own_target) {
            target_view = &dest_copy->get_view();
        }
    }

    begin_pass(color, depth);
    CommandBuffer &c = cmd();
    c.set_opaque_state();
    c.set_program(gs->program);
    c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    c.set_cull_mode(VK_CULL_MODE_NONE);
    VkViewport vp = {0.0f, 0.0f, float(color->width), float(color->height), 0.0f, 1.0f};
    c.set_viewport(vp);
    uint32_t x1 = std::min(sx1 + 1, color->width), y1 = std::min(sy1 + 1, color->height);
    if (sx0 >= x1 || sy0 >= y1) {
        gs->verts.clear();
        return;
    }
    c.set_scissor({{int32_t(sx0), int32_t(sy0)}, {x1 - sx0, y1 - sy0}});
    c.set_texture(0, 0, *raw_view, StockSampler::NearestClamp);
    c.set_texture(0, 1, *target_view, StockSampler::NearestClamp);
    c.set_texture(0, 3, dest_copy ? dest_copy->get_view() : gs->dummy_rgba->get_view(), StockSampler::NearestClamp);
    uint32_t *clut_data = c.allocate_typed_constant_data<uint32_t>(0, 2, 256);
    if (want_clut) {
        memcpy(clut_data, clut, sizeof(clut));
    } else {
        memset(clut_data, 0, 1024);
    }

    // depth
    if (depth) {
        c.set_depth_test(true, !zmsk);
        c.set_depth_compare(ztst == 2 ? VK_COMPARE_OP_GREATER_OR_EQUAL : ztst == 3 ? VK_COMPARE_OP_GREATER
                                                                                   : VK_COMPARE_OP_ALWAYS);
    } else {
        c.set_depth_test(false, false);
    }
    // blending
    if (abe && !shader_blend) {
        float constants[4] = {float(fix) / 128.0f, float(fix) / 128.0f, float(fix) / 128.0f, 128.0f / 255.0f};
        c.set_blend_enable(true);
        c.set_blend_constants(constants);
        bool alpha_factor = (push.flags & F_ALPHA_FACTOR) != 0;
        c.set_blend_factors(blend.src, alpha_factor ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE, blend.dst,
                            VK_BLEND_FACTOR_ZERO);
        c.set_blend_op(blend.op, VK_BLEND_OP_ADD);
    } else {
        c.set_blend_enable(false);
    }
    uint32_t write_mask = 0;
    for (int i = 0; i < 4; i++) {
        if (shader_blend || ((fbmsk >> (i * 8)) & 0xFF) != 0xFF) {
            write_mask |= 1u << i;
        }
    }
    if (fpsm == GSN_PSMCT24) {
        write_mask &= 7;
    }

    // the vertices
    size_t bytes = gs->verts.size() * sizeof(Vtx);
    void *vdata = c.allocate_vertex_data(0, bytes, sizeof(Vtx));
    memcpy(vdata, gs->verts.data(), bytes);
    c.set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vtx, x));
    c.set_vertex_attrib(1, 0, VK_FORMAT_R32_UINT, offsetof(Vtx, z));
    c.set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Vtx, rgba));
    c.set_vertex_attrib(3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vtx, s));
    c.set_vertex_attrib(4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vtx, fog));

    // the alpha test: pixels that pass are written whole; those that fail, as AFAIL says
    struct Pass {
        uint32_t select; // 0 all, 1 passing, 2 failing
        uint32_t mask;
        bool zwrite;
    } passes[2];
    int npasses = 0;
    bool zwrite = depth && !zmsk;
    if (!ate || atst == 1) {
        passes[npasses++] = {0, write_mask, zwrite};
    } else {
        if (atst != 0) {
            passes[npasses++] = {1, write_mask, zwrite};
        }
        uint32_t fail_mask = 0;
        bool fail_z = false;
        switch (afail) {
        case 0: // KEEP
            break;
        case 1: // FB_ONLY
            fail_mask = write_mask;
            break;
        case 2: // ZB_ONLY
            fail_z = zwrite;
            break;
        case 3: // RGB_ONLY
            fail_mask = write_mask & 7;
            break;
        }
        if (fail_mask || fail_z) {
            passes[npasses++] = {atst == 0 ? 0u : 2u, fail_mask, fail_z};
        }
    }
    for (int i = 0; i < npasses; i++) {
        Push p = push;
        p.flags |= passes[i].select << F_PASS_SHIFT;
        c.set_color_write_mask(passes[i].mask);
        if (depth) {
            c.set_depth_test(true, passes[i].zwrite);
        }
        c.push_constants(&p, 0, sizeof(p));
        c.draw(uint32_t(gs->verts.size()));
        gsn_stats.draws++;
    }
    color->dirty = true;
    if (depth && zwrite) {
        depth->dirty = true;
    }
    gsn_stats.primitives += gs->verts.size() / 3;
    gs->verts.clear();
}

// ---- Primitives ---------------------------------------------------------------------------------
Vtx make_vtx(const QVertex &q, uint32_t prim, const QVertex &colour_from) {
    uint32_t ctxt = (prim >> 9) & 1;
    uint64_t offset = gs->reg[R_XYOFFSET_1 + ctxt];
    Vtx v;
    v.x = (float(int32_t(q.x) - int32_t(bits(offset, 0, 16)))) / 16.0f;
    v.y = (float(int32_t(q.y) - int32_t(bits(offset, 32, 16)))) / 16.0f;
    v.z = q.z;
    v.rgba = colour_from.rgba;
    if ((prim >> 8) & 1) { // FST: UV in texels
        v.s = float(q.u) / 16.0f;
        v.t = float(q.v) / 16.0f;
        v.q = q.q;
    } else {
        v.s = q.s;
        v.t = q.t;
        v.q = q.q;
    }
    v.fog = float(q.fog);
    return v;
}

void add_triangle(const QVertex &a, const QVertex &b, const QVertex &c) {
    uint32_t prim = gs->batch_prim;
    bool gouraud = (prim >> 3) & 1;
    gs->verts.push_back(make_vtx(a, prim, gouraud ? a : c));
    gs->verts.push_back(make_vtx(b, prim, gouraud ? b : c));
    gs->verts.push_back(make_vtx(c, prim, c));
}

void add_sprite(const QVertex &a, const QVertex &b) {
    uint32_t prim = gs->batch_prim;
    // a sprite's colour, Z and fog are its second vertex's; its texture coordinates run corner to corner
    QVertex c[4] = {b, b, b, b};
    c[0].x = a.x, c[0].y = a.y, c[0].s = a.s, c[0].t = a.t, c[0].u = a.u, c[0].v = a.v;
    c[1].x = b.x, c[1].y = a.y, c[1].s = b.s, c[1].t = a.t, c[1].u = b.u, c[1].v = a.v;
    c[2].x = a.x, c[2].y = b.y, c[2].s = a.s, c[2].t = b.t, c[2].u = a.u, c[2].v = b.v;
    c[3] = b;
    Vtx v[4];
    for (int i = 0; i < 4; i++) {
        v[i] = make_vtx(c[i], prim, b);
    }
    gs->verts.push_back(v[0]);
    gs->verts.push_back(v[1]);
    gs->verts.push_back(v[2]);
    gs->verts.push_back(v[1]);
    gs->verts.push_back(v[3]);
    gs->verts.push_back(v[2]);
}

void vertex_kick(uint32_t address, uint64_t value) {
    bool fog = address == R_XYZF2 || address == R_XYZF3;
    bool draw = address == R_XYZF2 || address == R_XYZ2;
    QVertex v;
    v.x = bits(value, 0, 16);
    v.y = bits(value, 16, 16);
    v.z = fog ? bits(value, 32, 24) : uint32_t(value >> 32);
    v.fog = fog ? bits(value, 56, 8) : bits(gs->reg[R_FOG], 56, 8);
    v.rgba = uint32_t(gs->reg[R_RGBAQ]);
    v.q = as_float(uint32_t(gs->reg[R_RGBAQ] >> 32));
    v.s = as_float(uint32_t(gs->reg[R_ST]));
    v.t = as_float(uint32_t(gs->reg[R_ST] >> 32));
    v.u = bits(gs->reg[R_UV], 0, 14);
    v.v = bits(gs->reg[R_UV], 16, 14);

    uint32_t prim = effective_prim();
    if (gs->verts.empty()) {
        gs->batch_prim = prim;
    }
    uint32_t type = prim & 7;
    QVertex *q = gs->queue;
    switch (type) {
    case 3: // triangles
        q[gs->queue_count++] = v;
        if (gs->queue_count == 3) {
            if (draw) {
                add_triangle(q[0], q[1], q[2]);
            }
            gs->queue_count = 0;
        }
        break;
    case 4: // a strip
        if (gs->queue_count == 3) {
            q[0] = q[1];
            q[1] = q[2];
            gs->queue_count = 2;
        }
        q[gs->queue_count++] = v;
        if (gs->queue_count == 3 && draw) {
            add_triangle(q[0], q[1], q[2]);
        }
        break;
    case 5: // a fan
        if (gs->queue_count == 3) {
            q[1] = q[2];
            gs->queue_count = 2;
        }
        q[gs->queue_count++] = v;
        if (gs->queue_count == 3 && draw) {
            add_triangle(q[0], q[1], q[2]);
        }
        break;
    case 6: // sprites
        q[gs->queue_count++] = v;
        if (gs->queue_count == 2) {
            if (draw) {
                add_sprite(q[0], q[1]);
            }
            gs->queue_count = 0;
        }
        break;
    default: { // points and lines: not used by the game
        static bool told;
        if (!told) {
            fprintf(stderr, "[gsn] PRIM %u (points or lines) is not drawn\n", type);
            told = true;
        }
        gs->queue_count = 0;
        break;
    }
    }
}

// ---- Transfers ----------------------------------------------------------------------------------
struct TransferRect {
    uint32_t bp, bw, psm, x, y, w, h;
};

TransferRect destination_rect() {
    uint64_t bitbltbuf = gs->reg[R_BITBLTBUF], trxpos = gs->reg[R_TRXPOS], trxreg = gs->reg[R_TRXREG];
    return {bits(bitbltbuf, 32, 14), bits(bitbltbuf, 48, 6), bits(bitbltbuf, 56, 6), bits(trxpos, 32, 11),
            bits(trxpos, 48, 11), bits(trxreg, 0, 12), bits(trxreg, 32, 12)};
}

void rect_pages(const TransferRect &r, uint32_t *lo, uint32_t *count) {
    buffer_pages(r.bp, r.bw, r.psm, r.x + r.w, r.y + r.h, lo, count);
}

// The rectangle has just been written in the CPU's VRAM: the targets over it, the same shape as it,
// take it too; the others were written back and dropped before
void sync_targets(const TransferRect &r) {
    for (auto &t : gs->targets) {
        if (!t->depth && t->bp == r.bp && t->bw == r.bw && psm_layout(t->psm) == psm_layout(r.psm)) {
            load_target_rect(*t, r.x, r.y, r.w, r.h);
        }
    }
}

// Before the rectangle is written in the CPU's VRAM: the targets over it in another shape go back to
// VRAM and stop being targets
void prepare_write(const TransferRect &r) {
    uint32_t lo, count;
    rect_pages(r, &lo, &count);
    for (size_t i = 0; i < gs->targets.size();) {
        Target *t = gs->targets[i].get();
        uint32_t tlo, tcount;
        target_pages(*t, &tlo, &tcount);
        bool same = !t->depth && t->bp == r.bp && t->bw == r.bw && psm_layout(t->psm) == psm_layout(r.psm);
        if (pages_overlap(lo, count, tlo, tcount)) {
            if (same) {
                // the rest of the target stays newer than VRAM; the rectangle, after the write, both
                i++;
                continue;
            }
            write_back(*t);
            drop_target(t);
            continue;
        }
        i++;
    }
}

void begin_transfer(uint32_t dir) {
    uint64_t bitbltbuf = gs->reg[R_BITBLTBUF], trxpos = gs->reg[R_TRXPOS], trxreg = gs->reg[R_TRXREG];
    gs->transfer_dir = dir;
    if (dir == 0) {
        TransferRect r = destination_rect();
        prepare_write(r);
        gsn_upload_begin(&gs->upload, bitbltbuf, trxpos, trxreg);
        gsn_stats.uploads++;
    } else if (dir == 1) {
        // local to host: what the GPU drew there, back first
        uint32_t sbp = bits(bitbltbuf, 0, 14), sbw = bits(bitbltbuf, 16, 6), spsm = bits(bitbltbuf, 24, 6);
        uint32_t lo, count;
        buffer_pages(sbp, sbw, spsm, bits(trxpos, 0, 11) + bits(trxreg, 0, 12), bits(trxpos, 16, 11) + bits(trxreg, 32, 12),
                     &lo, &count);
        claim_pages(lo, count, nullptr, false);
        uint32_t w = bits(trxreg, 0, 12), h = bits(trxreg, 32, 12);
        size_t qwords = (size_t(w) * h * gsn_psm_bits(spsm) + 127) / 128;
        gs->readback.assign(qwords * 16, 0);
        gsn_vram_read_image(gs->vram.data(), bitbltbuf, trxpos, trxreg, gs->readback.data(), qwords);
        gs->readback_at = 0;
    } else if (dir == 2) {
        // local to local
        uint32_t sbp = bits(bitbltbuf, 0, 14), sbw = bits(bitbltbuf, 16, 6), spsm = bits(bitbltbuf, 24, 6);
        uint32_t sx = bits(trxpos, 0, 11), sy = bits(trxpos, 16, 11);
        TransferRect d = destination_rect();
        uint32_t w = d.w, h = d.h;
        // the frame copied by the GPU: from a target, into one, when the shapes allow
        Target *src = find_target(sbp, sbw, spsm);
        if (src && src->dirty && !src->depth && psm_layout(spsm) == psm_layout(d.psm) && sx + w <= src->width &&
            sy + h <= src->height && d.x == sx && d.y == sy && d.bw == sbw) {
            uint32_t lo, count;
            rect_pages(d, &lo, &count);
            Target *dst = find_target(d.bp, d.bw, d.psm);
            if (!dst || dst->width < src->width || dst->height < d.y + h) {
                // the destination becomes a target of its own, its pixels from VRAM first
                dst = get_target(d.bp, d.bw, d.psm, src->width, std::max(d.y + h, dst ? dst->height : 0u));
            }
            claim_pages(lo, count, dst, true);
            // claim_pages may have dropped and re-made nothing of src: find it again
            src = find_target(sbp, sbw, spsm);
            dst = find_target(d.bp, d.bw, d.psm);
            if (src && dst) {
                end_pass();
                transfer_barrier();
                cmd().copy_image(*dst->image, *src->image, {int32_t(d.x), int32_t(d.y), 0}, {int32_t(sx), int32_t(sy), 0},
                                 {w, h, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
                transfer_barrier();
                dst->dirty = true;
                gsn_stats.target_copies++;
                gs->transfer_dir = 3;
                return;
            }
        }
        uint32_t lo, count;
        buffer_pages(sbp, sbw, spsm, sx + w, sy + h, &lo, &count);
        claim_pages(lo, count, nullptr, false);
        prepare_write(d);
        if (gsn_vram_copy(gs->vram.data(), bitbltbuf, trxpos, trxreg)) {
            uint32_t dlo, dcount;
            rect_pages(d, &dlo, &dcount);
            pages_written(dlo, dcount);
        }
        sync_targets(d);
        gs->transfer_dir = 3;
    } else {
        gs->transfer_dir = 3;
    }
}

void image_data(const uint8_t *data, size_t bytes) {
    if (gs->transfer_dir != 0 || !gs->upload.active) {
        return;
    }
    uint32_t lo = 0, hi = 0;
    if (gsn_upload_data(&gs->upload, gs->vram.data(), data, bytes, &lo, &hi)) {
        pages_written(lo, hi - lo + 1);
    }
    gsn_stats.upload_bytes += bytes;
    if (!gs->upload.active) {
        sync_targets(destination_rect());
        gs->transfer_dir = 3;
    }
}

// ---- Register writes ----------------------------------------------------------------------------
// TEX1's MTBA: a TEX0 written makes MIPTBP1 itself, levels 1 to 3 packed after level 0, each a quarter
// of the one before and half its buffer width
void mip_addresses(uint32_t ctxt) {
    if (!bits(gs->reg[R_TEX1_1 + ctxt], 9, 1)) {
        return;
    }
    uint64_t tex0 = gs->reg[R_TEX0_1 + ctxt];
    uint32_t psm = bits(tex0, 20, 6);
    uint32_t w = 1u << bits(tex0, 26, 4), h = 1u << bits(tex0, 30, 4);
    // a block of the format, in pixels
    uint32_t bw = psm == GSN_PSMT4 ? 32 : psm == GSN_PSMT8 || psm_is_16(psm) ? 16 : 8;
    uint32_t bh = psm == GSN_PSMT8 ? 16 : psm == GSN_PSMT4 ? 16 : 8;
    uint32_t base = bits(tex0, 0, 14);
    uint32_t blocks = (w / bw) * (h / bh);
    uint32_t row = w / 64;
    uint64_t miptbp1 = 0;
    for (int l = 0; l < 3; l++) {
        base += blocks;
        blocks /= 4;
        row /= 2;
        miptbp1 |= (uint64_t(base & 0x3FFF) | (uint64_t(row & 0x3F) << 14)) << (l * 20);
    }
    gs->reg[R_MIPTBP1_1 + ctxt] = miptbp1;
}

void gs_write(uint32_t address, uint64_t value) {
    switch (address) {
    case R_XYZF2:
    case R_XYZ2:
    case R_XYZF3:
    case R_XYZ3:
        vertex_kick(address, value);
        return;
    case R_RGBAQ:
    case R_ST:
    case R_UV:
    case R_FOG:
        gs->reg[address] = value;
        return;
    case R_PRIM:
        gs->queue_count = 0;
        if ((value & 0x7FF) != (gs->reg[R_PRIM] & 0x7FF)) {
            uint64_t old = gs->reg[R_PRIM];
            gs->reg[R_PRIM] = value & 0x7FF;
            if ((effective_prim() & 0x7F8) != (gs->batch_prim & 0x7F8) && !gs->verts.empty()) {
                gs->reg[R_PRIM] = old;
                flush_batch();
                gs->reg[R_PRIM] = value & 0x7FF;
            }
        }
        return;
    case R_TEX0_1:
    case R_TEX0_2: {
        uint64_t old = gs->reg[address];
        uint64_t clut_before = gs->clut_version;
        // the CLUT is loaded as TEX0 is written; a batch drawn before keeps its own
        uint16_t saved[512];
        memcpy(saved, gs->clut, sizeof(saved));
        clut_load(value);
        bool changed = (old & ~(uint64_t(7) << 61)) != (value & ~(uint64_t(7) << 61)) || gs->clut_version != clut_before;
        if (changed && !gs->verts.empty()) {
            uint16_t now[512];
            memcpy(now, gs->clut, sizeof(now));
            memcpy(gs->clut, saved, sizeof(saved));
            flush_batch();
            memcpy(gs->clut, now, sizeof(now));
        }
        gs->reg[address] = value;
        mip_addresses(address - R_TEX0_1);
        return;
    }
    case R_TEX2_1:
    case R_TEX2_2: {
        // TEX2: TEX0's PSM and CLUT fields only
        uint32_t tex0 = address - R_TEX2_1 + R_TEX0_1;
        const uint64_t mask = (uint64_t(0x3F) << 20) | (~uint64_t(0) << 37);
        gs_write(tex0, (gs->reg[tex0] & ~mask) | (value & mask));
        return;
    }
    case R_TEXFLUSH:
    case 0x0F: // NOP
    case 0x7E: // the port's widescreen mark (gs.c)
        return;
    case R_SIGNAL:
    case R_FINISH:
    case R_LABEL:
        return;
    case R_XYOFFSET_1:
    case R_XYOFFSET_2:
        // applied to each vertex as it is queued
        gs->reg[address] = value;
        return;
    case R_BITBLTBUF:
    case R_TRXPOS:
    case R_TRXREG:
        gs->reg[address] = value;
        return;
    case R_TRXDIR:
        flush_batch();
        gs->reg[address] = value;
        begin_transfer(uint32_t(value & 3));
        return;
    case R_HWREG:
        image_data(reinterpret_cast<const uint8_t *>(&value), 8);
        return;
    }
    if (gs->reg[address & 0xFF] != value) {
        flush_batch();
        gs->reg[address & 0xFF] = value;
    }
}

void packed(uint32_t desc, const uint64_t q[2]) {
    switch (desc) {
    case 0x0:
        gs_write(R_PRIM, q[0] & 0x7FF);
        break;
    case 0x1:
        gs_write(R_RGBAQ, (q[0] & 0xFF) | ((q[0] >> 24) & 0xFF00) | ((q[1] & 0xFF) << 16) |
                              ((q[1] >> 8) & 0xFF000000u) | (uint64_t(gs->internal_q) << 32));
        break;
    case 0x2:
        gs->internal_q = uint32_t(q[1]);
        gs_write(R_ST, q[0]);
        break;
    case 0x3:
        gs_write(R_UV, (q[0] & 0x3FFF) | (((q[0] >> 32) & 0x3FFF) << 16));
        break;
    case 0x4: {
        uint64_t v = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | (((q[1] >> 4) & 0xFFFFFF) << 32) |
                     (((q[1] >> 36) & 0xFF) << 56);
        gs_write(((q[1] >> 47) & 1) ? R_XYZF3 : R_XYZF2, v);
        break;
    }
    case 0x5: {
        uint64_t v = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | ((q[1] & 0xFFFFFFFFu) << 32);
        gs_write(((q[1] >> 47) & 1) ? R_XYZ3 : R_XYZ2, v);
        break;
    }
    case 0xA:
        gs_write(R_FOG, ((q[1] >> 36) & 0xFF) << 56);
        break;
    case 0xC: {
        uint64_t v = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | (((q[1] >> 4) & 0xFFFFFF) << 32) |
                     (((q[1] >> 36) & 0xFF) << 56);
        gs_write(R_XYZF3, v);
        break;
    }
    case 0xD: {
        uint64_t v = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | ((q[1] & 0xFFFFFFFFu) << 32);
        gs_write(R_XYZ3, v);
        break;
    }
    case 0xE:
        gs_write(uint32_t(q[1] & 0xFF), q[0]);
        break;
    case 0xF:
        break;
    default: // TEX0, CLAMP: the register's own value
        gs_write(desc, q[0]);
        break;
    }
}

void gif(int path, const uint8_t *data, size_t bytes) {
    GifPath *p = &gs->paths[path];
    size_t count = bytes / 16;
    for (size_t i = 0; i < count; i++) {
        uint64_t q[2];
        memcpy(q, data + i * 16, 16);
        if (p->left == 0) {
            uint64_t tag = q[0];
            uint32_t nloop = uint32_t(tag & 0x7FFF);
            p->eop = uint32_t((tag >> 15) & 1);
            p->flg = uint32_t((tag >> 58) & 3);
            p->nreg = uint32_t(tag >> 60);
            if (p->nreg == 0) {
                p->nreg = 16;
            }
            p->regs = q[1];
            p->reg = 0;
            if (p->flg == 0) {
                p->left = nloop * p->nreg;
                if ((tag >> 46) & 1) {
                    gs_write(R_PRIM, (tag >> 47) & 0x7FF);
                }
            } else if (p->flg == 1) {
                p->left = (nloop * p->nreg + 1) / 2;
                p->dwords = nloop * p->nreg;
            } else {
                p->left = nloop;
            }
            p->in_packet = !(p->eop && p->left == 0);
            gs->internal_q = 0x3F800000u; // a tag sets Q to 1
            continue;
        }
        if (p->flg == 0) {
            packed(uint32_t(p->regs >> (p->reg * 4)) & 0xF, q);
            p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            p->left--;
        } else if (p->flg == 1) {
            for (int half = 0; half < 2 && p->dwords; half++, p->dwords--) {
                uint32_t desc = uint32_t(p->regs >> (p->reg * 4)) & 0xF;
                if (desc < 0xE) {
                    gs_write(desc, q[half]);
                }
                p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            }
            p->left--;
        } else {
            // IMAGE: as many qwords as there are, at once
            size_t n = std::min(size_t(p->left), count - i);
            image_data(data + i * 16, n * 16);
            p->left -= uint32_t(n);
            i += n - 1;
        }
        if (p->left == 0 && p->eop) {
            p->in_packet = 0;
        }
    }
}

// ---- Scanout ------------------------------------------------------------------------------------
// A rectangle of a buffer as RGBA: from its target if it is one, else from the CPU's VRAM
void read_buffer(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 uint32_t *out) {
    Target *t = find_target(bp, bw, psm);
    if (t && x + w <= t->width && y + h <= t->height) {
        const uint8_t *pixels = read_image(*t->image, x, y, w, h);
        for (size_t i = 0; i < size_t(w) * h; i++) {
            uint32_t v;
            memcpy(&v, pixels + i * 4, 4);
            out[i] = v | 0xFF000000u;
        }
        done_reading();
        return;
    }
    uint32_t lo, count;
    buffer_pages(bp, bw, psm, x + w, y + h, &lo, &count);
    claim_pages(lo, count, nullptr, false);
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            uint32_t v = vram_to_target_pixel(gsn_vram_read(gs->vram.data(), psm, bp, bw, x + i, y + j), psm);
            out[size_t(j) * w + i] = v | 0xFF000000u;
        }
    }
}

bool display_rect(const uint64_t *privileged, GsnRect *r) {
    // a register every 16 bytes: PMODE, SMODE1, SMODE2, ..., DISPFB1, DISPLAY1, DISPFB2, DISPLAY2
    uint64_t pmode = privileged[0], smode2 = privileged[2 * 2];
    int circuit = (pmode & 2) ? 2 : (pmode & 1) ? 1 : 0;
    if (!circuit) {
        return false;
    }
    uint64_t dispfb = privileged[(circuit == 2 ? 9 : 7) * 2], display = privileged[(circuit == 2 ? 10 : 8) * 2];
    r->bp = bits(dispfb, 0, 9) * 32;
    r->bw = bits(dispfb, 9, 6);
    r->psm = bits(dispfb, 15, 5);
    r->x = bits(dispfb, 32, 11);
    r->y = bits(dispfb, 43, 11);
    r->w = (bits(display, 32, 12) + 1) / (bits(display, 23, 4) + 1);
    r->h = bits(display, 44, 11) + 1;
    if ((smode2 & 3) == 3) {
        r->h /= 2; // field mode: a field's picture
    }
    return r->w > 0 && r->h > 0 && r->w <= 1024 && r->h <= 1024;
}

void make_program() {
    ResourceLayout vert = {};
    vert.input_mask = 0x1F;
    vert.push_constant_size = sizeof(Push);
    ResourceLayout frag = {};
    frag.output_mask = 1;
    frag.push_constant_size = sizeof(Push);
    frag.sets[0].sampled_image_mask = (1u << 0) | (1u << 1) | (1u << 3);
    frag.sets[0].uniform_buffer_mask = 1u << 2;
    frag.sets[0].fp_mask = (1u << 1) | (1u << 3);
    for (int b : {0, 1, 2, 3}) {
        frag.sets[0].meta[b].array_size = 1;
    }
    Shader *vs = gs->device->request_shader(gsn_spv_gs_vert, sizeof(gsn_spv_gs_vert), &vert);
    Shader *fs = gs->device->request_shader(gsn_spv_gs_frag, sizeof(gsn_spv_gs_frag), &frag);
    gs->program = gs->device->request_program(vs, fs);
}

void reset_gs() {
    memset(gs->reg, 0, sizeof(gs->reg));
    gs->internal_q = 0x3F800000u;
    memset(gs->clut, 0, sizeof(gs->clut));
    gs->clut_cbp[0] = gs->clut_cbp[1] = UINT32_MAX;
    gs->queue_count = 0;
    memset(gs->paths, 0, sizeof(gs->paths));
    memset(&gs->upload, 0, sizeof(gs->upload));
    gs->transfer_dir = 3;
}

} // namespace

// ---- The C interface ----------------------------------------------------------------------------
extern "C" int gsn_open(void *shared_context) {
    gs = new Gs();
    gs->vram.assign(GSN_VRAM_BYTES, 0);
    memset(gs->page_written, 0, sizeof(gs->page_written));
    gs->vram_writes = 1;
    reset_gs();

    if (shared_context) {
        gs->context = static_cast<Context *>(shared_context);
    } else {
        if (!Context::init_loader(nullptr)) {
            fprintf(stderr, "[gsn] no Vulkan loader\n");
            return 0;
        }
        gs->context = new Context;
        gs->context->set_num_thread_indices(1);
        if (!gs->context->init_instance_and_device(nullptr, 0, nullptr, 0, 0)) {
            fprintf(stderr, "[gsn] no Vulkan device\n");
            return 0;
        }
        gs->own_context = true;
    }
    const char *level = getenv("KF4_GSN_LOG");
    log_level = level ? atoi(level) : 0;
    gs->device = new Device;
    gs->device->set_context(*gs->context);
    gs->device->init_frame_contexts(2);
    if (shared_context) {
        port_gs_backend_share_queue(gs->device); // one queue, two devices: one lock
    }
    make_program();

    gs->dummy_raw = make_image(1, 1, VK_FORMAT_R8_UINT, 1, false);
    gs->dummy_rgba = make_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1, false);
    cmd().clear_image(*gs->dummy_raw, {});
    cmd().clear_image(*gs->dummy_rgba, {});
    transfer_barrier();
    submit(true);
    fprintf(stderr, "[gsn] the native GS on %s\n", gs->device->get_gpu_properties().deviceName);
    return 1;
}

extern "C" void gsn_gif(int path, const void *data, size_t bytes) {
    gif(path, static_cast<const uint8_t *>(data), bytes);
}

extern "C" void gsn_read_fifo(void *dst, uint32_t qwords) {
    size_t bytes = size_t(qwords) * 16;
    size_t have = gs->readback.size() > gs->readback_at ? gs->readback.size() - gs->readback_at : 0;
    size_t n = std::min(bytes, have);
    memcpy(dst, gs->readback.data() + gs->readback_at, n);
    memset(static_cast<uint8_t *>(dst) + n, 0, bytes - n);
    gs->readback_at += n;
}

extern "C" int gsn_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame) {
    flush_batch();
    // a register every 16 bytes: PMODE, SMODE1, SMODE2, ..., DISPFB1, DISPLAY1, DISPFB2, DISPLAY2
    auto preg = [&](int n) { return privileged[n * 2]; };
    uint64_t pmode = preg(0);
    uint64_t smode2 = preg(2);
    int circuit = (pmode & 2) ? 2 : (pmode & 1) ? 1 : 0;
    int ok = 0;
    if (circuit) {
        uint64_t dispfb = preg(circuit == 2 ? 9 : 7), display = preg(circuit == 2 ? 10 : 8);
        uint32_t fbp = bits(dispfb, 0, 9) * 32, fbw = bits(dispfb, 9, 6), psm = bits(dispfb, 15, 5);
        uint32_t dbx = bits(dispfb, 32, 11), dby = bits(dispfb, 43, 11);
        uint32_t magh = bits(display, 23, 4) + 1;
        uint32_t w = (bits(display, 32, 12) + 1) / magh, h = bits(display, 44, 11) + 1;
        bool interlaced = smode2 & 1, field_mode = (smode2 >> 1) & 1;
        if (interlaced && field_mode) {
            h /= 2;
        }
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024 && size_t(w) * h * (field_mode ? 2 : 1) <= PORT_GS_FRAME_PIXELS) {
            if (interlaced && field_mode) {
                // each field its own picture, woven with the one before: kept every field, wanted or not
                int f = field & 1;
                gs->fields[f].resize(size_t(w) * h);
                read_buffer(fbp, fbw, psm, dbx, dby, w, h, gs->fields[f].data());
                if (log_level >= 2) {
                    uint64_t hash = 1469598103934665603ull;
                    for (uint32_t v : gs->fields[f]) {
                        hash = (hash ^ v) * 1099511628211ull;
                    }
                    GSN_LOG(2, "field %d (CSR %d) from fbp %#x: %016llx\n", f, field, fbp, (unsigned long long)hash);
                }
                if (gs->fields[f ^ 1].size() != gs->fields[f].size()) {
                    gs->fields[f ^ 1] = gs->fields[f];
                }
                if (want_pixels) {
                    for (uint32_t y = 0; y < h * 2; y++) {
                        memcpy(frame->rgba + size_t(y) * w, gs->fields[y & 1].data() + size_t(y / 2) * w, w * 4);
                    }
                    frame->width = w;
                    frame->height = h * 2;
                    ok = 1;
                }
            } else if (want_pixels) {
                read_buffer(fbp, fbw, psm, dbx, dby, w, h, frame->rgba);
                frame->width = w;
                frame->height = h;
                ok = 1;
            }
        }
    }
    submit(false);
    gs->device->next_frame_context();
    return ok;
}

extern "C" int gsn_display_rect(const uint64_t *privileged, GsnRect *rect) {
    return display_rect(privileged, rect);
}

extern "C" void gsn_read_rect(const GsnRect *r, uint32_t *rgba) {
    flush_batch();
    read_buffer(r->bp, r->bw, r->psm, r->x, r->y, r->w, r->h, rgba);
    submit(false);
}

extern "C" void gsn_decode_rect(const uint8_t *vram, const GsnRect *r, uint32_t *rgba) {
    for (uint32_t j = 0; j < r->h; j++) {
        for (uint32_t i = 0; i < r->w; i++) {
            uint32_t v = gsn_vram_read(vram, r->psm, r->bp, r->bw, r->x + i, r->y + j);
            rgba[size_t(j) * r->w + i] = vram_to_target_pixel(v, r->psm);
        }
    }
}

extern "C" void gsn_write_rect(const GsnRect *r, const uint32_t *rgba) {
    flush_batch();
    Target *t = find_target(r->bp, r->bw, r->psm);
    if (t && r->x + r->w <= t->width && r->y + r->h <= t->height) {
        end_pass();
        transfer_barrier();
        uint32_t *out = static_cast<uint32_t *>(cmd().update_image(*t->image, {int32_t(r->x), int32_t(r->y), 0},
                                                                   {r->w, r->h, 1}, r->w, r->h,
                                                                   {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}));
        memcpy(out, rgba, size_t(r->w) * r->h * 4);
        transfer_barrier();
        return;
    }
    uint32_t lo, count;
    buffer_pages(r->bp, r->bw, r->psm, r->x + r->w, r->y + r->h, &lo, &count);
    claim_pages(lo, count, nullptr, true);
    for (uint32_t j = 0; j < r->h; j++) {
        for (uint32_t i = 0; i < r->w; i++) {
            gsn_vram_write(gs->vram.data(), r->psm, r->bp, r->bw, r->x + i, r->y + j,
                           target_pixel_to_vram(rgba[size_t(j) * r->w + i], r->psm));
        }
    }
    pages_written(lo, count);
}

extern "C" const void *gsn_vram(void) {
    flush_batch();
    for (auto &t : gs->targets) {
        write_back(*t);
    }
    return gs->vram.data();
}

extern "C" void gsn_capture(void (*emit)(uint32_t type, const void *data, size_t size)) {
    emit(KF4GS_VRAM, gsn_vram(), GSN_VRAM_BYTES);
    std::vector<Kf4GsReg> regs;
    static const uint32_t saved[] = {0x00, 0x01, 0x02, 0x03, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x14, 0x15, 0x18,
                                     0x19, 0x1A, 0x1B, 0x1C, 0x22, 0x34, 0x35, 0x36, 0x37, 0x3B, 0x3D, 0x40,
                                     0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C,
                                     0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53};
    for (uint32_t a : saved) {
        regs.push_back({a, 0, gs->reg[a]});
    }
    regs.push_back({KF4GS_REG_INTERNAL_Q, 0, gs->internal_q});
    regs.push_back({KF4GS_REG_CACHED_CBP0, 0, gs->clut_cbp[0]});
    regs.push_back({KF4GS_REG_CACHED_CBP1, 0, gs->clut_cbp[1]});
    emit(KF4GS_GS_REGS, regs.data(), regs.size() * sizeof(Kf4GsReg));
}

extern "C" int gsn_restore(uint32_t type, const void *data, size_t size) {
    switch (type) {
    case KF4GS_VRAM:
        if (size != GSN_VRAM_BYTES) {
            return 0;
        }
        flush_batch();
        end_pass();
        gs->targets.clear();
        gs->textures.clear();
        memcpy(gs->vram.data(), data, GSN_VRAM_BYTES);
        pages_written(0, vram_pages);
        reset_gs();
        return 1;
    case KF4GS_GS_REGS: {
        const Kf4GsReg *regs = static_cast<const Kf4GsReg *>(data);
        for (size_t n = 0; n < size / sizeof(Kf4GsReg); n++) {
            if (regs[n].address < 256) {
                gs->reg[regs[n].address] = regs[n].value;
            } else if (regs[n].address == KF4GS_REG_INTERNAL_Q) {
                gs->internal_q = uint32_t(regs[n].value);
            }
            // the CLUT buffer is not in a capture: the next TEX0 that compares loads it again
        }
        gs->clut_cbp[0] = gs->clut_cbp[1] = UINT32_MAX;
        return 1;
    }
    case KF4GS_GS_PRIV:
    case KF4GS_GS_HISTORY:
        return 1;
    }
    return 0;
}
