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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "context.hpp"
#include "device.hpp"
#include "thread_id.hpp"

extern "C" {
#include "port/gs_capture_format.h"
#include "port/platform.h"
#include "gsn.h"
#include "gsn_vram.h"
}
#include "gsn_shaders.h"

using namespace Vulkan;

GsnStats gsn_stats;

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
    uint32_t tscale; // a texture that is a render target: how many of its pixels a GS pixel is, across
                     // (bits 0-15) and down (16-31)
    float lod_bias;  // --native-lod: added to the mipmap level the GS picks (only to which level)
};
static_assert(sizeof(Push) == 88, "Push is the shaders' push constant block");

enum : uint32_t {
    F_TME = 0x1, F_FST = 0x2, F_TFX_SHIFT = 2, F_TCC = 0x10, F_FGE = 0x20, F_SHADER_BLEND = 0x40,
    F_SRC_SHIFT = 8, F_ALPHA_FACTOR = 0x1000, F_PASS_SHIFT = 13, F_DATE = 0x8000, F_PREMUL = 0x10000,
    F_PREMUL_CEIL = 0x20000,
};
enum : uint32_t {
    SRC_CLUT = 0, SRC_CT32 = 1, SRC_CT24 = 2, SRC_CT16 = 3, SRC_TARGET_CT32 = 4, SRC_TARGET_CT24 = 5,
    SRC_TARGET_CT16 = 6,
};
enum : uint32_t {
    M_FBA = 0x1, M_COLCLAMP = 0x2, M_DST16 = 0x4, M_DST24 = 0x8, M_PABE = 0x10, M_SCANMSK_SHIFT = 5, M_SCALE_SHIFT = 7,
    M_ZCLAMP = 0x400, M_SCALE_X_SHIFT = 11, M_ANISO_SHIFT = 14, M_HIGH = 0x20000,
};

// ---- Render targets and textures ----------------------------------------------------------------
struct Target {
    uint32_t bp, bw, psm; // bp in 256-byte blocks
    uint32_t width, height; // in GS pixels: the image is `sx` times as wide and `sy` times as high
    uint32_t sx, sy;
    ImageHandle image;
    bool depth;
    bool dirty; // newer than the CPU's VRAM (somewhere: dirty_pages says where)
    bool stale; // a page of it is another target's (stale_pages: shares_pages below)
    // which of its pages (target_pages' order: rows of bw pages, when the target is `regular`) are
    // newer than the CPU's VRAM; the others hold what VRAM does
    std::vector<uint8_t> dirty_pages;
    std::vector<uint8_t> stale_pages;
    uint64_t used;
    // --native-msaa: the multisampled image the 3D is drawn into (made when first drawn so), and which
    // of the two has pixels the other does not (msaa_settle, msaa_ready)
    ImageHandle ms;
    bool ms_newer, image_newer;
};

struct Texture {
    uint32_t tbp, tbw, psm, tw, th, levels;
    uint32_t mip_tbp[7], mip_tbw[7];
    ImageHandle image;
    uint32_t page_lo[7], page_count[7];
    uint64_t decoded; // the VRAM write count when it was decoded
    uint64_t hash;    // of the bytes it was decoded from (texture_hash)
    uint64_t used;
    // --native-cutout-aa: how many of its texels (all levels) have each value of the byte their alpha
    // comes from (a palette texture's index, a 32-bit texture's alpha): which alphas it has, how often
    uint32_t texels[256];
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
    Vtx queued[3]; // the same, as the shaders take them (made as each is queued)
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
    Program *program_zclamp = nullptr; // (shaders/gs_zclamp.frag: Z kept within the format per pixel)
    Program *program_zclamp_sample = nullptr; // (shaders/gs_zclamp_sample.frag: the same at every sample)
    Program *program_ms_load = nullptr, *program_ms_load_depth = nullptr, *program_ms_depth = nullptr; // (msaa_*)
    Program *program_deinterlace = nullptr; // (the scanout's: shaders/deinterlace.frag)
    Program *program_narrow = nullptr;      // (--native-colour high's scanout: shaders/narrow.frag)
    CommandBufferHandle cmd;
    ImageHandle dummy_raw, dummy_rgba, snapshot, scanout;
    BufferHandle readback_buffer;
    VkDeviceSize readback_size = 0;
    std::vector<uint8_t> narrowed; // read_image's pixels of a 16-bit image, rounded to 8 bits
    std::vector<std::unique_ptr<Target>> targets;
    std::vector<std::unique_ptr<Texture>> textures;
    uint64_t tick = 0;
    // the render pass under way
    Target *pass_color = nullptr;
    Target *pass_depth = nullptr;
    bool pass_ms = false; // drawing into the targets' multisampled images
    bool in_pass = false;

    // ---- the batch being gathered: triangles since the last register write
    std::vector<Vtx> verts;
    uint32_t batch_prim = 0; // PRIM's attributes (PRMODE's when PRMODECONT says)
    bool batch_sprites = false; // the batch is sprites

    // the last field shown (field mode: the picture is woven from the last two)
    std::vector<uint32_t> fields[2];
    uint32_t field_width = 0, field_height = 0;
    // vblanks left that a frame-mode picture is read a field at a time: a primitive over more than 400
    // lines drawn with SCANMSK (into one field's lines only) sets it to 4 (scanmsk_seen, "The scanout")
    uint32_t field_scan_vblanks = 0;
    // the displayed buffer as it was at the last vblanks read a field at a time, the newest first (the
    // deinterlace's history; filled: how many are there)
    ImageHandle history[4];
    uint32_t history_filled = 0;
};

Gs *gs;
// --native-scale: the render targets' size, in pixels a GS pixel across (sx) and down (sy); 1 and 1
// are the PS2's. sx is larger than sy for --native-widescreen. Changed at a vblank (gsn_set_scale),
// when every target goes back to VRAM and comes again at the new size.
uint32_t target_sx = 1, target_sy = 1;
volatile uint32_t wanted_scale = 1 | 1 << 8; // sx | sy << 8
// --native-lod (gsn_set_lod): which mipmap level a texture is read at, at a larger --native-scale
volatile int lod_mode = GSN_LOD_SCALE;
// --native-aniso (gsn_set_aniso): log2 of the most taps along a mipmapped texture's footprint (0: off)
volatile int aniso_log2 = 0;
// --native-msaa: the samples a pixel of the 3D's targets has (1: none, the PS2's picture; 2, 4, 8).
// Changed at a vblank (gsn_set_msaa), as the scale is.
uint32_t target_samples = 1;
volatile uint32_t wanted_samples = 1;
// --native-cutout-aa (gsn_set_cutout_aa): with MSAA, the cut-outs (flush_batch) shaded at every
// sample; sample_shading, whether the GPU can (sampleRateShading)
volatile int cutout_aa_mode = GSN_CUTOUT_AA_OFF;
bool sample_shading = false;

// --native-colour (gsn_set_colour): the 32-bit colour targets at 8 bits a channel, as the GS has them
// (GSN_COLOUR_EXACT), or 16 (GSN_COLOUR_HIGH: "--native-colour high" below). Changed at a vblank, as the
// scale is.
uint32_t target_colour = GSN_COLOUR_EXACT;
volatile uint32_t wanted_colour = GSN_COLOUR_EXACT;

// The colour targets' format: 16-bit UNORM holds every 8-bit value exactly (v * 257), so what the GS's
// arithmetic writes there reads back as it is
VkFormat colour_format() {
    return target_colour == GSN_COLOUR_HIGH ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
}

bool wide(const Image &image) {
    return image.get_format() == VK_FORMAT_R16G16B16A16_UNORM;
}

// An 8-bit RGBA pixel as a 16-bit one, each channel v * 257
uint64_t widen(uint32_t v) {
    uint64_t x = v;
    x = (x | x << 16) & 0x0000FFFF0000FFFFull;
    x = (x | x << 8) & 0x00FF00FF00FF00FFull; // each byte in a 16-bit lane
    return x | x << 8;
}

// A 16-bit RGBA pixel rounded to 8 bits a channel
uint32_t narrow(uint64_t v) {
    uint32_t out = 0;
    for (int c = 0; c < 4; c++) {
        uint32_t x = uint32_t(v >> (c * 16)) & 0xFFFF;
        out |= ((x * 255u + 32767u) / 65535u) << (c * 8);
    }
    return out;
}

// n pixels into an image's update_image memory at pixel `at`, as the image holds them
void put_pixels(void *out, const Image &image, size_t at, const uint32_t *rgba, size_t n) {
    if (wide(image)) {
        uint64_t *o = static_cast<uint64_t *>(out) + at;
        for (size_t i = 0; i < n; i++) {
            o[i] = widen(rgba[i]);
        }
    } else {
        memcpy(static_cast<uint32_t *>(out) + at, rgba, n * 4);
    }
}

// Time spent in a function, added to a counter of gsn_stats
struct Timer {
    uint64_t &total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit Timer(uint64_t &t) : total(t) {}
    ~Timer() {
        total += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    }
};
int log_level; // KF4_GSN_LOG: 1 says when targets are made and dropped, 2 every batch too, 3 each vblank's display registers

#define GSN_LOG(level, ...)                                                                                \
    do {                                                                                                   \
        if (log_level >= (level)) {                                                                        \
            fprintf(stderr, "[gsn] " __VA_ARGS__);                                                         \
        }                                                                                                  \
    } while (0)

void settle_upload(); // (the upload under way, written so far: below)

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
        gs->pass_ms = false;
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
    VkDeviceSize size = VkDeviceSize(w) * h * (wide(image) ? 8 : 4);
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
    const uint8_t *mapped =
        static_cast<const uint8_t *>(gs->device->map_host_buffer(*gs->readback_buffer, MEMORY_ACCESS_READ_BIT));
    if (!wide(image)) {
        return mapped;
    }
    // (--native-colour high: rounded to 8 bits)
    gs->narrowed.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        uint64_t v;
        memcpy(&v, mapped + i * 8, 8);
        uint32_t n = narrow(v);
        memcpy(gs->narrowed.data() + i * 4, &n, 4);
    }
    return gs->narrowed.data();
}

void done_reading() {
    gs->device->unmap_host_buffer(*gs->readback_buffer, MEMORY_ACCESS_READ_BIT);
}

// ---- --native-msaa ------------------------------------------------------------------------------
// With MSAA on (target_samples above 1), the 3D is drawn into multisampled images: a target drawn into
// with a Z buffer gets one (Target::ms, as large as its image), and so does its Z buffer. Every draw
// with a Z buffer goes to them; so does a draw without one into a target whose multisampled image is
// newer than its image, unless it reads that target as its texture (flush_batch). Everything else uses
// the image as before: draws without Z (the 2D, the passes over the whole frame), textures read from
// targets, write-backs, frame copies, uploads, the scanout. The two are kept in step lazily, at most
// one of them newer: before the image is read or written, a newer multisampled image is resolved
// into it (msaa_settle: colour, each pixel the mean of its samples; Z, its first sample); before the
// multisampled image is drawn into, a newer image is copied into every sample of it (msaa_ready).
// Draws into the multisampled images are shaded once a pixel, as without MSAA: only coverage and Z
// are per sample, so the pixels inside a primitive are what they are without MSAA, and its edges are
// smoothed when resolved. A draw that blends in the shader reads the target's resolved pixels
// (snapshot_of), which keeps a blended pixel's samples as one colour.

// Granite's cache of render passes does not tell them apart by their attachments' sample counts: a
// pass into multisampled images names its one subpass, where the passes into the images have the
// default (so the two kinds are not mixed up), and one run uses one sample count (msaa_samples_used).
// (Set on the GS's thread, read by gsn_set_msaa on the sender's or the overlay's; msaa_samples_have,
// the counts this GPU has, set by gsn_open, so gsn_set_msaa needs nothing of Granite's.)
std::atomic<uint32_t> msaa_samples_used{0};
uint32_t msaa_samples_have = 1;
// The GPU's kind (gsn_gpu_kind), set by gsn_open: --native-scale auto's most
int gpu_kind = GSN_GPU_OTHER;

void msaa_subpass(RenderPassInfo &rp, RenderPassInfo::Subpass &subpass) {
    subpass = {};
    subpass.num_color_attachments = rp.num_color_attachments;
    subpass.color_attachments[0] = 0;
    subpass.depth_stencil_mode = rp.depth_stencil ? RenderPassInfo::DepthStencil::ReadWrite : RenderPassInfo::DepthStencil::None;
    rp.subpasses = &subpass;
    rp.num_subpasses = 1;
}

ImageHandle make_ms_image(uint32_t width, uint32_t height, bool depth) {
    msaa_samples_used = target_samples;
    ImageCreateInfo info = ImageCreateInfo::render_target(width, height, depth ? VK_FORMAT_D32_SFLOAT : colour_format());
    info.samples = VkSampleCountFlagBits(target_samples);
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    info.layout = ImageLayout::General;
    info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
    return gs->device->create_image(info, nullptr);
}

// One of the copying passes (shaders/msaa.vert): src read at each pixel of dst, written to its colour
// or (depth) its Z
void msaa_copy_pass(const Image &dst, const Image &src, Program *program, bool depth) {
    end_pass();
    RenderPassInfo rp;
    RenderPassInfo::Subpass subpass;
    if (depth) {
        rp.depth_stencil = &dst.get_view();
        rp.op_flags = RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
    } else {
        rp.num_color_attachments = 1;
        rp.color_attachments[0] = &dst.get_view();
        rp.store_attachments = 1;
    }
    if (dst.get_create_info().samples != VK_SAMPLE_COUNT_1_BIT) {
        msaa_subpass(rp, subpass);
    }
    CommandBuffer &c = cmd();
    c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    c.begin_render_pass(rp);
    c.set_opaque_state();
    c.set_program(program);
    c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    c.set_cull_mode(VK_CULL_MODE_NONE);
    c.set_depth_test(depth, depth);
    c.set_depth_compare(VK_COMPARE_OP_ALWAYS);
    c.set_texture(0, 0, src.get_view(), StockSampler::NearestClamp);
    c.draw(3);
    c.end_render_pass();
    c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

// A rectangle of a multisampled colour image resolved into a single-sampled one, at the same place
void msaa_resolve(const Image &dst, const Image &src, VkOffset3D at, VkExtent3D size) {
    end_pass();
    transfer_barrier();
    VkImageResolve region = {};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffset = at;
    region.dstOffset = at;
    region.extent = size;
    gs->device->get_device_table().vkCmdResolveImage(cmd().get_command_buffer(), src.get_image(),
                                                     src.get_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL), dst.get_image(),
                                                     dst.get_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), 1, &region);
    transfer_barrier();
    gsn_stats.ms_resolves++;
}

// The target's image made current: what was drawn into the multisampled image, resolved into it.
// (Called before anything reads or writes the image; nothing when MSAA is off.)
void msaa_settle(Target &t) {
    if (!t.ms_newer) {
        return;
    }
    if (t.depth) {
        msaa_copy_pass(*t.image, *t.ms, gs->program_ms_depth, true);
        gsn_stats.ms_resolves++;
    } else {
        msaa_resolve(*t.image, *t.ms, {0, 0, 0}, {t.image->get_width(), t.image->get_height(), 1});
    }
    t.ms_newer = false;
}

// The target's image has been written (a draw without MSAA, an upload, a copy): the multisampled
// image, if there is one, is behind it
void msaa_image_written(Target &t) {
    if (t.ms) {
        t.image_newer = true;
    }
}

// The target's multisampled image made, and current, to be drawn into
void msaa_ready(Target &t) {
    if (!t.ms) {
        t.ms = make_ms_image(t.image->get_width(), t.image->get_height(), t.depth);
        t.image_newer = true;
    }
    if (t.image_newer) {
        msaa_copy_pass(*t.ms, *t.image, t.depth ? gs->program_ms_load_depth : gs->program_ms_load, t.depth);
        t.image_newer = false;
        gsn_stats.ms_loads++;
    }
}

// The multisampled image let go (the target's image was made again at another size)
void msaa_forget(Target &t) {
    t.ms.reset();
    t.ms_newer = t.image_newer = false;
}

// The 2D covers the same GS pixels it does on the PS2, whole: drawn into a multisampled image
// (--native-msaa), or into a target larger than the PS2's (--native-scale, --native-widescreen). Its
// primitives are rectangles on the screen (sprites, and the menus' quads: two triangles of a strip
// with one Z), each triangle half of one: its corners are those of its bounding box. Such a
// triangle's corners are moved out to the edges of the GS pixels it covers (those whose integer point
// is inside: from ceil(x0) to ceil(x1) - 1 across, the same down), and its texture coordinates and fog
// carried along their slopes, so that each pixel's middle has what it had.
// (Left as they are, an edge GS pixel's samples, or its pixels of a larger target, on the far side
// of its integer point would not be covered: a rectangle over the whole frame would leave half of its
// first column and row as they were, and every box and letter of the menus would be blurred by half
// a pixel. At --native-scale 2, a picture drawn into a 128 x 128 target by a sprite from 0 to 127.5
// left the target's first column and row as they were: the sky's clouds on map 000 are such a
// picture, and showed it as bright lines where their texture repeats; the frame's passes over the
// whole picture left its first column and row: a line down the screen's left edge.) A triangle with a slant, or with its colour or Z not
// the same at its corners, is left as it is: the 3D. Returns whether it moved any.
bool flat_2d(std::vector<Vtx> &verts) {
    bool moved = false;
    for (size_t i = 0; i + 3 <= verts.size(); i += 3) {
        Vtx *v = &verts[i];
        if (v[0].z != v[1].z || v[0].z != v[2].z || v[0].rgba != v[1].rgba || v[0].rgba != v[2].rgba) {
            continue;
        }
        float x0 = std::min(v[0].x, std::min(v[1].x, v[2].x)), x1 = std::max(v[0].x, std::max(v[1].x, v[2].x));
        float y0 = std::min(v[0].y, std::min(v[1].y, v[2].y)), y1 = std::max(v[0].y, std::max(v[1].y, v[2].y));
        bool corners = x0 < x1 && y0 < y1;
        for (int k = 0; k < 3 && corners; k++) {
            corners = (v[k].x == x0 || v[k].x == x1) && (v[k].y == y0 || v[k].y == y1);
        }
        if (!corners) {
            continue;
        }
        // the attributes' slopes across and down (the plane through the three corners)
        double ax = double(v[1].x) - v[0].x, ay = double(v[1].y) - v[0].y;
        double bx = double(v[2].x) - v[0].x, by = double(v[2].y) - v[0].y;
        double det = ax * by - bx * ay;
        if (det == 0) {
            continue; // (two corners the same)
        }
        float *attr[3][4];
        for (int k = 0; k < 3; k++) {
            attr[k][0] = &v[k].s, attr[k][1] = &v[k].t, attr[k][2] = &v[k].q, attr[k][3] = &v[k].fog;
        }
        double dx[4], dy[4];
        for (int a = 0; a < 4; a++) {
            double da = double(*attr[1][a]) - *attr[0][a], db = double(*attr[2][a]) - *attr[0][a];
            dx[a] = (da * by - db * ay) / det;
            dy[a] = (db * ax - da * bx) / det;
        }
        for (int k = 0; k < 3; k++) {
            // the vertex shader adds half a GS pixel: ceil(x) - 0.5 puts the corner on the left edge of
            // GS pixel ceil(x) (at any scale, of its first pixel of the target)
            double nx = ceil(v[k].x) - 0.5, ny = ceil(v[k].y) - 0.5;
            for (int a = 0; a < 4; a++) {
                *attr[k][a] = float(*attr[k][a] + dx[a] * (nx - v[k].x) + dy[a] * (ny - v[k].y));
            }
            v[k].x = float(nx);
            v[k].y = float(ny);
        }
        moved = true;
    }
    return moved;
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

// A target whose pages are rows of bw pages from bp (on a page, and as wide as its buffer): each of
// its pages is a known 64-pixel-wide rectangle of it, so what is newer than VRAM is kept by page.
// (Otherwise all of a dirty target counts as dirty.)
bool regular(const Target &t) {
    return t.bp % 32 == 0 && t.bw > 0 && t.width == t.bw * 64;
}

// The target's pages, none newer than VRAM
void clear_dirty(Target &t) {
    uint32_t lo, count;
    target_pages(t, &lo, &count);
    t.dirty_pages.assign(count, 0);
    t.dirty = false;
    t.stale_pages.assign(count, 0);
    t.stale = false;
}

// Rows y0..y1-1 of the target drawn into (all of it, for one that is not regular)
void mark_dirty(Target &t, uint32_t y0, uint32_t y1) {
    t.dirty = true;
    if (!regular(t)) {
        std::fill(t.dirty_pages.begin(), t.dirty_pages.end(), 1);
        return;
    }
    y1 = std::min(y1, t.height);
    uint32_t ph = page_height(t.psm);
    for (uint32_t r = y0 / ph; y0 < y1 && r <= (y1 - 1) / ph; r++) {
        for (uint32_t c = 0; c < t.bw && size_t(r) * t.bw + c < t.dirty_pages.size(); c++) {
            t.dirty_pages[size_t(r) * t.bw + c] = 1;
        }
    }
}

// Whether a page of these is one of the target's newer than VRAM
bool dirty_in(const Target &t, uint32_t lo, uint32_t count) {
    if (!t.dirty) {
        return false;
    }
    uint32_t tlo, tcount;
    target_pages(t, &tlo, &tcount);
    if (!regular(t)) {
        return pages_overlap(lo, count, tlo, tcount);
    }
    for (uint32_t k = 0; k < tcount && k < t.dirty_pages.size(); k++) {
        if (t.dirty_pages[k] && pages_overlap(lo, count, (tlo + k) % vram_pages, 1)) {
            return true;
        }
    }
    return false;
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
    Timer timer(gsn_stats.ns_writeback);
    if (!t.dirty) {
        return;
    }
    msaa_settle(t);
    gsn_stats.writebacks++;
    GSN_LOG(1, "write back target bp %#x bw %u psm %#x %ux%u\n", t.bp, t.bw, t.psm, t.width, t.height);
    uint32_t lo, count;
    target_pages(t, &lo, &count);
    // the rows of pages with a page newer than VRAM, read; those pages written
    uint32_t ph = page_height(t.psm), row0 = 0, row1 = t.height;
    bool by_page = regular(t) && t.dirty_pages.size() == count;
    if (by_page) {
        uint32_t first = count, last = 0;
        for (uint32_t k = 0; k < count; k++) {
            if (t.dirty_pages[k]) {
                first = std::min(first, k);
                last = k;
            }
        }
        if (first == count) {
            t.dirty = false;
            return;
        }
        row0 = first / t.bw * ph;
        row1 = std::min(t.height, (last / t.bw + 1) * ph);
    }
    uint32_t sx = t.sx, sy = t.sy, pw = t.width * sx;
    const uint8_t *pixels = read_image(*t.image, 0, row0 * sy, pw, (row1 - row0) * sy);
    // (a CT24 target's alpha is VRAM's (CT24 draws do not write it) unless a CT32 draw into the same
    // buffer wrote it: the whole word goes back either way)
    uint32_t psm = t.depth ? t.psm : psm_layout(t.psm);
    std::vector<uint32_t> row(t.width);
    for (uint32_t y = row0; y < row1; y++) {
        // a run of pixels to go back: the row, or a page's part of it
        for (uint32_t x = 0; x < t.width;) {
            uint32_t n = t.width - x;
            if (by_page) {
                n = std::min(n, 64u);
                if (!t.dirty_pages[(y / ph) * t.bw + x / 64]) {
                    x += n;
                    continue;
                }
            }
            for (uint32_t i = 0; i < n; i++) {
                uint32_t v;
                // a scaled target: the pixel in the middle of the GS pixel's
                memcpy(&v, pixels + (size_t((y - row0) * sy + sy / 2) * pw + (x + i) * sx + sx / 2) * 4, 4);
                if (t.depth) {
                    float f;
                    memcpy(&f, &v, 4);
                    double z = double(f) / depth_scale(t.psm) + 0.5;
                    row[i] = z >= double(depth_max(t.psm)) ? depth_max(t.psm) : uint32_t(z);
                } else {
                    row[i] = target_pixel_to_vram(v, psm);
                }
            }
            gsn_vram_write_row(gs->vram.data(), psm, t.bp, t.bw, x, y, n, row.data());
            x += n;
        }
    }
    done_reading();
    if (by_page) {
        for (uint32_t k = 0; k < count; k++) {
            if (t.dirty_pages[k]) {
                pages_written((lo + k) % vram_pages, 1);
            }
        }
    } else {
        pages_written(lo, count);
    }
    clear_dirty(t);
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
    msaa_settle(t); // (the rest of the image stays)
    end_pass();
    transfer_barrier();
    VkImageAspectFlags aspect = t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t sx = t.sx, sy = t.sy, pw = w * sx, ph = h * sy;
    void *out = cmd().update_image(*t.image, {int32_t(x0 * sx), int32_t(y0 * sy), 0}, {pw, ph, 1}, pw, ph,
                                   {aspect, 0, 0, 1});
    std::vector<uint32_t> row(w), line(pw);
    std::vector<uint8_t> converted;
    for (uint32_t y = 0; y < h; y++) {
        // (a CT24 target holds VRAM's alpha too: write_back writes the whole word)
        gsn_vram_read_row(gs->vram.data(), t.depth ? t.psm : psm_layout(t.psm), t.bp, t.bw, x0, y0 + y, w, row.data());
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = row[x];
            if (t.depth) {
                float f = float(double(v) * depth_scale(t.psm));
                memcpy(&v, &f, 4);
            } else {
                v = vram_to_target_pixel(v, t.psm);
            }
            for (uint32_t i = 0; i < sx; i++) {
                line[x * sx + i] = v;
            }
        }
        // (the row made here, then copied: the upload's memory is not for reading)
        size_t row_bytes = size_t(pw) * (wide(*t.image) ? 8 : 4);
        converted.resize(row_bytes);
        put_pixels(converted.data(), *t.image, 0, line.data(), pw);
        for (uint32_t j = 0; j < sy; j++) {
            memcpy(static_cast<uint8_t *>(out) + size_t(y * sy + j) * row_bytes, converted.data(), row_bytes);
        }
    }
    transfer_barrier();
    msaa_image_written(t);
    // the pages wholly inside the rectangle hold what VRAM does now
    if (t.dirty && regular(t)) {
        uint32_t pgh = page_height(t.psm);
        // (the last row of pages may go past the target's height: it has only the rows above that)
        for (uint32_t r = (y0 + pgh - 1) / pgh; r * pgh < t.height && std::min((r + 1) * pgh, t.height) <= y0 + h; r++) {
            for (uint32_t c = (x0 + 63) / 64; (c + 1) * 64 <= x0 + w && c < t.bw; c++) {
                if (size_t(r) * t.bw + c < t.dirty_pages.size()) {
                    t.dirty_pages[size_t(r) * t.bw + c] = 0;
                }
            }
        }
        t.dirty = std::find(t.dirty_pages.begin(), t.dirty_pages.end(), 1) != t.dirty_pages.end();
    }
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
bool shares_pages(const Target &a, const Target &b);

// (sharer: targets that share pages with it (shares_pages) are left alone: take_pages moves pages
// between them)
void claim_pages(uint32_t lo, uint32_t count, const Target *keep, bool drop, const Target *sharer = nullptr) {
    for (size_t i = 0; i < gs->targets.size();) {
        Target *t = gs->targets[i].get();
        uint32_t tlo, tcount;
        target_pages(*t, &tlo, &tcount);
        if (t != keep && pages_overlap(lo, count, tlo, tcount) && !(sharer && shares_pages(*sharer, *t))) {
            if (drop) {
                write_back(*t);
                drop_target(t);
                continue;
            }
            // (a target whose pages here hold what VRAM does need not go back)
            if (dirty_in(*t, lo, count)) {
                write_back(*t);
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

// A colour buffer that starts a whole number of page rows from a colour target's start, as wide and in
// the same layout, is the target's memory from that row on: its pixel (x, y) is the target's pixel
// (x, y + *rows). (*rows below 0: the buffer starts above the target.)
bool row_alias(const Target &t, uint32_t bp, uint32_t bw, uint32_t psm, int32_t *rows) {
    if (t.depth || psm_is_depth(psm) || t.bw != bw || bw == 0 || psm_layout(t.psm) != psm_layout(psm)) {
        return false;
    }
    // (a colour format's page is 64 pixels across: a row of pages is bw pages, 32 blocks each)
    int32_t d = int32_t(bp) - int32_t(t.bp), row = int32_t(bw) * 32;
    if (d % row != 0) {
        return false;
    }
    *rows = d / row * int32_t(page_height(t.psm));
    return true;
}

// A target made again in a new size, its old picture copied in `rows` rows down: what was newer than
// VRAM in the old one is in the new one
void carry_dirty(Target &t, const Target &old, uint32_t rows) {
    if (!old.dirty) {
        return;
    }
    t.dirty = true;
    if (regular(t) && regular(old) && old.bw == t.bw && rows % page_height(t.psm) == 0) {
        size_t shift = size_t(rows / page_height(t.psm)) * t.bw;
        for (size_t k = 0; k < old.dirty_pages.size() && k + shift < t.dirty_pages.size(); k++) {
            t.dirty_pages[k + shift] |= old.dirty_pages[k];
        }
    } else {
        std::fill(t.dirty_pages.begin(), t.dirty_pages.end(), 1);
    }
}

// Two Z buffers in the same format at the same address, at different widths (map 008 and 009's
// reflection draws with the main Z buffer's address at 256 wide, each frame): a page is the same 8 KB
// in both, its pixels in the same places, so a page moves from one to the other as a GPU copy of its
// 64-pixel-wide rectangle, where going back through VRAM cost two read-backs a frame. A page is the
// other's (stale) in all of them but one.
bool shares_pages(const Target &a, const Target &b) {
    return &a != &b && a.depth && b.depth && psm_layout(a.psm) == psm_layout(b.psm) && regular(a) && regular(b) &&
           a.sx == b.sx && a.sy == b.sy && a.bw != b.bw; // (scaled alike: a page is the same rectangle)
}

// Pages of t from the targets it shares pages with, where they are theirs: copied in on the GPU, and
// theirs no more (only_stale: just the pages t has as stale; any of those nobody has, from VRAM)
void take_pages(Target &t, bool only_stale) {
    uint32_t tlo, tcount;
    target_pages(t, &tlo, &tcount);
    uint32_t tph = page_height(t.psm), sx = t.sx, sy = t.sy;
    struct Copy {
        Target *from;
        uint32_t k_from, k_to;
    };
    std::vector<Copy> copies;
    for (auto &o : gs->targets) {
        if (!shares_pages(t, *o)) {
            continue;
        }
        uint32_t olo, ocount;
        target_pages(*o, &olo, &ocount);
        if (!pages_overlap(tlo, tcount, olo, ocount)) {
            continue;
        }
        for (uint32_t k = 0; k < tcount; k++) {
            if (only_stale && !t.stale_pages[k]) {
                continue;
            }
            uint32_t ko = ((tlo + k) % vram_pages + vram_pages - olo) % vram_pages;
            if (ko >= ocount || o->stale_pages[ko]) {
                continue;
            }
            copies.push_back({o.get(), ko, k});
            t.dirty_pages[k] = o->dirty_pages[ko];
            t.stale_pages[k] = 0;
            o->dirty_pages[ko] = 0;
            o->stale_pages[ko] = 1;
            o->stale = true;
        }
    }
    if (!copies.empty()) {
        // --native-msaa: the pages move between the targets' images, so each is made current first
        // (Z resolved: its first sample), and t's multisampled image is behind it after
        msaa_settle(t);
        for (const Copy &c : copies) {
            msaa_settle(*c.from);
        }
        end_pass();
        transfer_barrier();
        const VkImageSubresourceLayers z = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        for (const Copy &c : copies) {
            Target &o = *c.from;
            uint32_t oph = page_height(o.psm);
            uint32_t ox = (c.k_from % o.bw) * 64, oy = (c.k_from / o.bw) * oph;
            uint32_t tx = (c.k_to % t.bw) * 64, ty = (c.k_to / t.bw) * tph;
            uint32_t h = std::min(std::min(oph, o.height - oy), t.height - ty);
            cmd().copy_image(*t.image, *o.image, {int32_t(tx * sx), int32_t(ty * sy), 0}, {int32_t(ox * sx), int32_t(oy * sy), 0},
                             {64 * sx, h * sy, 1}, z, z);
        }
        transfer_barrier();
        msaa_image_written(t);
        gsn_stats.target_copies += copies.size();
        for (auto &o : gs->targets) {
            o->dirty = std::find(o->dirty_pages.begin(), o->dirty_pages.end(), 1) != o->dirty_pages.end();
        }
    }
    // pages nobody else has: VRAM's
    if (only_stale) {
        for (uint32_t k = 0; k < tcount; k++) {
            if (t.stale_pages[k]) {
                t.stale_pages[k] = 0;
                load_target_rect(t, (k % t.bw) * 64, (k / t.bw) * tph, 64, tph);
            }
        }
    }
    t.stale = false;
    t.dirty = std::find(t.dirty_pages.begin(), t.dirty_pages.end(), 1) != t.dirty_pages.end();
}

// The target that is the buffer at bp, at least width x height, made (from the CPU's VRAM) or grown
Target *get_target(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t width, uint32_t height) {
    Target *t = find_target(bp, bw, psm);
    width = std::max(width, std::max(bw, 1u) * 64);
    if (t && t->stale) {
        take_pages(*t, true);
    }
    if (t && t->width >= width && t->height >= height) {
        t->psm = psm;
        t->used = gs->tick;
        return t;
    }
    VkFormat format = psm_is_depth(psm) ? VK_FORMAT_D32_SFLOAT : colour_format();
    if (!t) {
        // A buffer that starts some rows above a target and runs into it (a scrolling texture's
        // target, drawn into from a page row higher the next time): the target grows upwards to start
        // where the buffer does, instead of going back to VRAM to be made again in the new shape
        Target *below = nullptr;
        int32_t rows = 0;
        for (auto &o : gs->targets) {
            if (row_alias(*o, bp, bw, psm, &rows) && rows < 0 && int32_t(height) > -rows) {
                below = o.get();
                break;
            }
        }
        if (below) {
            uint32_t down = uint32_t(-rows), sx = below->sx, sy = below->sy;
            uint32_t new_w = std::max(width, below->width), new_h = std::max(height, down + below->height);
            GSN_LOG(1, "target bp %#x grows up to bp %#x: %ux%u\n", below->bp, bp, new_w, new_h);
            end_pass();
            msaa_settle(*below);
            Target old = *below;
            below->bp = bp;
            below->psm = psm;
            below->image = make_image(new_w * sx, new_h * sy, format, 1, true);
            msaa_forget(*below);
            below->width = new_w;
            below->height = new_h;
            clear_dirty(*below);
            uint32_t lo, count;
            target_pages(*below, &lo, &count);
            claim_pages(lo, count, below, true);
            // VRAM where the old picture is not, then the old picture
            load_target_rect(*below, 0, 0, new_w, down);
            load_target_rect(*below, 0, down + old.height, new_w, new_h - (down + old.height));
            load_target_rect(*below, old.width, down, new_w - old.width, old.height);
            cmd().copy_image(*below->image, *old.image, {0, int32_t(down * sy), 0}, {0, 0, 0},
                             {old.width * sx, old.height * sy, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                             {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
            transfer_barrier();
            carry_dirty(*below, old, down);
            below->used = gs->tick;
            return below;
        }
    }
    if (t) {
        // grown: the old picture kept, the new part from VRAM
        uint32_t new_w = std::max(width, t->width), new_h = std::max(height, t->height);
        ImageHandle image = make_image(new_w * t->sx, new_h * t->sy, format, 1, true);
        end_pass();
        msaa_settle(*t);
        Target old = *t;
        t->psm = psm;
        t->image = image;
        msaa_forget(*t);
        t->width = new_w;
        t->height = new_h;
        clear_dirty(*t);
        // what the old target did not cover comes from VRAM: the whole of it first, then the old on top
        uint32_t lo, count;
        target_pages(*t, &lo, &count);
        claim_pages(lo, count, t, true, t);
        load_target_rect(*t, 0, 0, new_w, new_h);
        VkImageAspectFlags aspect = t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        cmd().copy_image(*t->image, *old.image, {0, 0, 0}, {0, 0, 0}, {old.width * t->sx, old.height * t->sy, 1},
                         {aspect, 0, 0, 1}, {aspect, 0, 0, 1});
        transfer_barrier();
        carry_dirty(*t, old, 0);
        take_pages(*t, false);
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
    clear_dirty(*made);
    made->used = gs->tick;
    made->sx = target_sx;
    made->sy = target_sy;
    made->image = make_image(width * made->sx, height * made->sy, format, 1, true);
    GSN_LOG(1, "new target bp %#x bw %u psm %#x %ux%u\n", bp, bw, psm, width, height);
    uint32_t lo, count;
    target_pages(*made, &lo, &count);
    // whatever else was drawn over these pages goes back to VRAM first, and stops being a target
    // (but for a Z buffer sharing its pages, which hands them over)
    claim_pages(lo, count, nullptr, true, made.get());
    load_target_rect(*made, 0, 0, width, height);
    gs->targets.push_back(std::move(made));
    take_pages(*gs->targets.back(), false);
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
std::unordered_map<uint64_t, std::array<uint32_t, 256>> decoded_texels; // (Texture::texels, by the same hash)
std::deque<uint64_t> decoded_order;
size_t decoded_pool_bytes;

size_t image_bytes(const Image &image) {
    size_t texel = image.get_format() == VK_FORMAT_R8_UINT ? 1 : image.get_format() == VK_FORMAT_R16_UINT ? 2 : 4;
    // levels after the first add a third at most
    return size_t(image.get_width()) * image.get_height() * texel * (image.get_create_info().levels > 1 ? 4 : 3) / 3;
}

void decode_texture(Texture &tex) {
    Timer timer(gsn_stats.ns_decode);
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
        memcpy(tex.texels, decoded_texels[hash].data(), sizeof(tex.texels));
        gsn_stats.texture_reuses++;
        return;
    }
    tex.image = make_image(1u << tex.tw, 1u << tex.th, format, tex.levels, false);
    decoded_pool[hash] = tex.image;
    decoded_order.push_back(hash);
    decoded_pool_bytes += image_bytes(*tex.image);
    // the oldest let go beyond 2,048 of them or 256 MB (a texture in use keeps its image all the same)
    while (decoded_order.size() > 2048 || decoded_pool_bytes > (256u << 20)) {
        auto oldest = decoded_pool.find(decoded_order.front());
        if (oldest != decoded_pool.end()) {
            decoded_pool_bytes -= image_bytes(*oldest->second);
            decoded_pool.erase(oldest);
            decoded_texels.erase(decoded_order.front());
        }
        decoded_order.pop_front();
    }
    end_pass();
    transfer_barrier();
    memset(tex.texels, 0, sizeof(tex.texels));
    for (uint32_t l = 0; l < tex.levels; l++) {
        uint32_t w = std::max(1u, (1u << tex.tw) >> l), h = std::max(1u, (1u << tex.th) >> l);
        void *out = cmd().update_image(*tex.image, {0, 0, 0}, {w, h, 1}, w, h, {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1});
        uint32_t bp = tex.mip_tbp[l], bw = tex.mip_tbw[l];
        std::vector<uint32_t> row(w);
        for (uint32_t y = 0; y < h; y++) {
            size_t at = size_t(y) * w;
            if (format == VK_FORMAT_R32_UINT) {
                uint32_t *texel = static_cast<uint32_t *>(out) + at;
                gsn_vram_read_row(gs->vram.data(), tex.psm, bp, bw, 0, y, w, texel);
                for (uint32_t x = 0; x < w; x++) {
                    tex.texels[texel[x] >> 24]++;
                }
                continue;
            }
            gsn_vram_read_row(gs->vram.data(), tex.psm, bp, bw, 0, y, w, row.data());
            for (uint32_t x = 0; x < w; x++) {
                if (format == VK_FORMAT_R8_UINT) {
                    static_cast<uint8_t *>(out)[at + x] = uint8_t(row[x]);
                    tex.texels[row[x] & 0xFF]++;
                } else {
                    static_cast<uint16_t *>(out)[at + x] = uint16_t(row[x]);
                }
            }
        }
        gsn_stats.texture_bytes += size_t(w) * h;
    }
    memcpy(decoded_texels[hash].data(), tex.texels, sizeof(tex.texels));
    transfer_barrier();
    gsn_stats.texture_decodes++;
}

// The textures by what they are (texture_key), for the lookup each batch makes
std::unordered_map<uint64_t, Texture *> texture_index;

uint64_t texture_key(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, uint32_t levels,
                     const uint32_t *mip_tbp, const uint32_t *mip_tbw) {
    uint64_t k = uint64_t(tbp) | uint64_t(tbw) << 14 | uint64_t(psm) << 20 | uint64_t(tw) << 26 | uint64_t(th) << 30 |
                 uint64_t(levels) << 34;
    for (uint32_t l = 1; l < levels; l++) {
        k = (k ^ (uint64_t(mip_tbp[l]) | uint64_t(mip_tbw[l]) << 14)) * 0x100000001B3ull;
    }
    return k;
}

bool same_texture(const Texture &t, uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, uint32_t levels,
                  const uint32_t *mip_tbp, const uint32_t *mip_tbw) {
    return t.tbp == tbp && t.tbw == tbw && t.psm == psm && t.tw == tw && t.th == th && t.levels == levels &&
           memcmp(t.mip_tbp, mip_tbp, levels * 4) == 0 && memcmp(t.mip_tbw, mip_tbw, levels * 4) == 0;
}

Texture *get_texture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, uint32_t levels,
                     const uint32_t *mip_tbp, const uint32_t *mip_tbw) {
    uint64_t key = texture_key(tbp, tbw, psm, tw, th, levels, mip_tbp, mip_tbw);
    auto found = texture_index.find(key);
    if (found != texture_index.end() && same_texture(*found->second, tbp, tbw, psm, tw, th, levels, mip_tbp, mip_tbw)) {
        Texture *t = found->second;
        t->used = gs->tick;
        if (t->decoded != gs->vram_writes && !texture_current(*t)) {
            decode_texture(*t);
        }
        return t;
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
    Texture *made = tex.get();
    if (found != texture_index.end()) {
        texture_index.erase(found); // (a key shared by two textures: the newer one is indexed)
    }
    texture_index[key] = made;
    gs->textures.push_back(std::move(tex));
    // a few hundred textures at most are in use at once: drop the oldest beyond that
    if (gs->textures.size() > 1024) {
        auto oldest = std::min_element(gs->textures.begin(), gs->textures.end(),
                                       [](const std::unique_ptr<Texture> &a, const std::unique_ptr<Texture> &b) {
                                           return a->used < b->used;
                                       });
        if (oldest->get() != made) {
            Texture *t = oldest->get();
            auto at = texture_index.find(texture_key(t->tbp, t->tbw, t->psm, t->tw, t->th, t->levels, t->mip_tbp, t->mip_tbw));
            if (at != texture_index.end() && at->second == t) {
                texture_index.erase(at);
            }
            gs->textures.erase(oldest);
        }
    }
    return made;
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
    uint32_t premul; // 0; or the shader gives Cs * C >> 7 (F_PREMUL), rounded up for a subtraction (F_PREMUL_CEIL)
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
    // Cs * C added to Cd or taken from it (glows, fire, shadows): the shader works out Cs * C >> 7 as
    // the GS does, Vulkan adds it or takes it away, so the result is the GS's exactly, whatever C is
    if (c != 1 && a == CS && b == ZERO && (d == CD || d == ZERO)) {
        out->op = VK_BLEND_OP_ADD;
        out->src = VK_BLEND_FACTOR_ONE;
        out->dst = d == CD ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        out->premul = F_PREMUL;
        return true;
    }
    if (c != 1 && a == ZERO && b == CS && (d == CD || d == ZERO)) {
        // (0 - Cs) C >> 7 is -(Cs C / 128) rounded down: Cs C / 128 rounded up, taken away
        out->op = VK_BLEND_OP_REVERSE_SUBTRACT;
        out->src = VK_BLEND_FACTOR_ONE;
        out->dst = d == CD ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        out->premul = F_PREMUL | F_PREMUL_CEIL;
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

// (ms: into the targets' multisampled images, --native-msaa)
void begin_pass(Target *color, Target *depth, bool ms = false) {
    if (gs->in_pass && gs->pass_color == color && gs->pass_depth == depth && gs->pass_ms == ms) {
        return;
    }
    end_pass();
    RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = ms ? &color->ms->get_view() : &color->image->get_view();
    rp.load_attachments = 1;
    rp.store_attachments = 1;
    if (depth) {
        rp.depth_stencil = ms ? &depth->ms->get_view() : &depth->image->get_view();
        rp.op_flags = RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
    }
    RenderPassInfo::Subpass subpass;
    if (ms) {
        msaa_subpass(rp, subpass);
    }
    cmd().barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    cmd().begin_render_pass(rp);
    gs->in_pass = true;
    gs->pass_color = color;
    gs->pass_depth = depth;
    gs->pass_ms = ms;
    gsn_stats.passes++;
}

// A copy of the target, for a draw that reads what it draws over
// (box: only the target's pixels a draw can cover, x0 y0 x1 y1 in GS pixels; NULL: all of it)
const Image &snapshot_of(Target &t, const int32_t *box = nullptr) {
    uint32_t tw = t.width * t.sx, th = t.height * t.sy;
    if (!gs->snapshot || gs->snapshot->get_format() != t.image->get_format()) {
        gs->snapshot.reset();
    }
    if (!gs->snapshot || gs->snapshot->get_width() < tw || gs->snapshot->get_height() < th) {
        uint32_t w = std::max(tw, gs->snapshot ? gs->snapshot->get_width() : 0u);
        uint32_t h = std::max(th, gs->snapshot ? gs->snapshot->get_height() : 0u);
        gs->snapshot = make_image(w, h, t.image->get_format(), 1, false);
    }
    VkOffset3D at = {0, 0, 0};
    VkExtent3D size = {tw, th, 1};
    if (box) {
        int32_t sx = int32_t(t.sx), sy = int32_t(t.sy);
        int32_t x0 = std::max(box[0], 0) * sx, y0 = std::max(box[1], 0) * sy;
        int32_t x1 = std::min(box[2] * sx, int32_t(tw)), y1 = std::min(box[3] * sy, int32_t(th));
        if (x1 <= x0 || y1 <= y0) {
            return *gs->snapshot; // (nothing of it on the target: no copy, and the render pass goes on)
        }
        at = {x0, y0, 0};
        size = {uint32_t(x1 - x0), uint32_t(y1 - y0), 1};
    }
    if (t.ms_newer) {
        // --native-msaa: the target's resolved pixels
        msaa_resolve(*gs->snapshot, *t.ms, at, size);
        return *gs->snapshot;
    }
    end_pass();
    transfer_barrier();
    cmd().copy_image(*gs->snapshot, *t.image, at, at, size, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    transfer_barrier();
    gsn_stats.target_copies++;
    return *gs->snapshot;
}

// Whether two triangles' insides overlap, so that a pixel could be drawn by both: no edge of either
// has the other wholly on its far side (the separating axis test). Triangles that share an edge, or
// only touch, do not; nor does one with no area, which draws nothing. (In doubles: exact for 12.4.)
bool triangles_overlap(const Vtx *a, const Vtx *b) {
    float ax0 = std::min(a[0].x, std::min(a[1].x, a[2].x)), ax1 = std::max(a[0].x, std::max(a[1].x, a[2].x));
    float bx0 = std::min(b[0].x, std::min(b[1].x, b[2].x)), bx1 = std::max(b[0].x, std::max(b[1].x, b[2].x));
    float ay0 = std::min(a[0].y, std::min(a[1].y, a[2].y)), ay1 = std::max(a[0].y, std::max(a[1].y, a[2].y));
    float by0 = std::min(b[0].y, std::min(b[1].y, b[2].y)), by1 = std::max(b[0].y, std::max(b[1].y, b[2].y));
    if (ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0) {
        return false;
    }
    for (int k = 0; k < 2; k++) {
        const Vtx *p = k ? b : a, *q = k ? a : b;
        double area = (double(p[1].x) - p[0].x) * (double(p[2].y) - p[0].y) -
                      (double(p[1].y) - p[0].y) * (double(p[2].x) - p[0].x);
        if (area == 0) {
            return false;
        }
        for (int i = 0; i < 3; i++) {
            const Vtx &e0 = p[i], &e1 = p[(i + 1) % 3];
            bool apart = true;
            for (int j = 0; j < 3 && apart; j++) {
                double side = (double(e1.x) - e0.x) * (double(q[j].y) - e0.y) - (double(e1.y) - e0.y) * (double(q[j].x) - e0.x);
                apart = (area > 0 ? side : -side) <= 0;
            }
            if (apart) {
                return false;
            }
        }
    }
    return true;
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
    Timer timer(gsn_stats.ns_flush);
    settle_upload();
    if (gs->verts.empty()) {
        return;
    }
    gs->tick++;
    // what the decisions below need of the vertices, in one pass over them: their Z's range, the
    // largest alpha, the rows they reach
    uint32_t zmin = UINT32_MAX, zmax = 0, as_max = 0, as_min = 255;
    float vy0 = gs->verts[0].y, vy1 = vy0;
    for (const Vtx &v : gs->verts) {
        zmin = std::min(zmin, v.z);
        zmax = std::max(zmax, v.z);
        as_max = std::max(as_max, v.rgba >> 24);
        as_min = std::min(as_min, v.rgba >> 24);
        vy0 = std::min(vy0, v.y);
        vy1 = std::max(vy1, v.y);
    }
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
    if (!bits(test, 16, 1)) {
        // ZTE 0 (undefined on the GS): the Z buffer is neither tested nor written, as paraLLEl-GS has
        // it. The movies draw each field with ZTE 0 and ZBUF at 0x1180, the movie's own picture:
        // writing Z there wrote zeros over the frame being shown (black and cyan bands at the top)
        zmsk = true;
    }

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
        always_passes = std::min(zmin, depth_max(zpsm)) == depth_max(zpsm);
    }
    bool need_depth = !(always_passes && !any_zwrite);
    // The rows the targets need: to the scissor's bottom; for a draw with Z, only to the last row its
    // vertices reach when that is higher. The menus' scissor runs a row past their 224-line field
    // buffers (0-224, inclusive), and targets made 225 rows tall from it reached each other (the second
    // field buffer's last row is in the Z buffer's first page): as the two attachments are made as big
    // as each other, they could not be targets together, and the field's clear and backdrop were drawn
    // without Z, which the brightness sprite of the field before had left at its largest: the
    // backdrop failed the Z test, and every other line of the Equipment and Journal screens was black.
    // Those draws reach row 223 (a sprite to 223.5 covers row 223 last), as on the GS.
    uint32_t need_h = sy1 + 1;
    if (need_depth) {
        need_h = std::min(need_h, uint32_t(std::max(0.0f, floorf(vy1))) + 1);
    }
    Target *color = get_target(fbp, fbw, fpsm, fbw * 64, need_h);
    Target *depth = nullptr;
    if (need_depth && color->height > need_h) {
        // a colour target taller than the draw needs (one drawn over both frame buffers once, say) that
        // runs into the Z buffer: back to VRAM, and made again as tall as the draw needs
        uint32_t clo, ccount, zlo, zcount;
        target_pages(*color, &clo, &ccount);
        buffer_pages(zbp, fbw, zpsm, fbw * 64, need_h, &zlo, &zcount);
        if (pages_overlap(clo, ccount, zlo, zcount)) {
            write_back(*color);
            drop_target(color);
            color = get_target(fbp, fbw, fpsm, fbw * 64, need_h);
        }
    }
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
            color = get_target(fbp, fbw, fpsm, fbw * 64, need_h);
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
                (bits(gs->reg[R_PABE], 0, 1) ? M_PABE : 0) | (bits(gs->reg[R_SCANMSK], 0, 2) << M_SCANMSK_SHIFT);
    if (wide(*color->image) && !dst16) {
        push.misc |= M_HIGH; // --native-colour high: the colours' arithmetic in floats
    }
    push.flags = (fge ? F_FGE : 0);

    // the texture
    const ImageView *raw_view = &gs->dummy_raw->get_view();
    const ImageView *target_view = &gs->dummy_rgba->get_view();
    bool reads_own_target = false;
    bool reads_target = false; // a feedback pass: the texture is a render target
    uint32_t clut[256];
    bool want_clut = false;
    uint32_t tex_alpha_max = 255; // the largest alpha a texel can have, where it is known
    // --native-cutout-aa: the alphas the texture's texels have, from the least to the most (only of a
    // texture decoded from VRAM; cut_texels false where they are not known)
    bool cut_texels = false;
    uint32_t cut_lo = 255, cut_hi = 0;
    uint64_t cut_clear = 0, cut_all = 0;
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
        // The GS picks a mipmap level from Q, the distance, not from the pixels a texel covers: at a
        // larger --native-scale a surface has more pixels at the same distance, and the level the PS2
        // picked for 640x448 is too coarse for them (further away, the picture blurs where the PS2's
        // was sharp). GSN_LOD_SCALE takes the level log2 of the scale finer (of the smaller of the two
        // scales: --native-widescreen's across is the larger), the detail the GS would show at that
        // resolution; GSN_LOD_FULL level 0 everywhere; GSN_LOD_PS2 the GS's own. At 1x GSN_LOD_SCALE is
        // the GS's own; GSN_LOD_FULL is level 0 at 1x too.
        uint32_t finer = std::min(target_sx, target_sy);
        int mode = lod_mode;
        push.lod_bias = mode == GSN_LOD_FULL ? -64.0f : mode == GSN_LOD_SCALE ? -log2f(float(finer)) : 0.0f;
        push.misc |= uint32_t(aniso_log2) << M_ANISO_SHIFT; // (--native-aniso: gs_frag.h, sample_texture)

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
            // A texture whose rows the batch reads run past the target's is decoded from VRAM, the
            // targets' pages back there first: the rows past are VRAM's, or another target's. (The
            // rows read, from the vertices' V, or T/Q: a frame read as a 1024 x 512 texture reads
            // only the frame's rows. A pixel samples above its V's largest, which is at a vertex
            // when Q stays above 0; bilinear filtering's row past that has no weight.)
            uint32_t rows = 1u << th;
            float vmin = 0.0f, vmax = 0.0f;
            for (const Vtx &v : gs->verts) {
                float tv = fst ? v.t : v.q > 0.0f ? v.t / v.q * float(rows) : -1.0f;
                vmin = std::min(vmin, tv);
                vmax = std::max(vmax, tv);
            }
            if (vmin >= 0.0f && vmax < float(rows)) {
                rows = std::max(1u, uint32_t(ceilf(vmax)));
            }
            if (y0 + rows > t->height) {
                break;
            }
            source = t.get();
            push.src_offset[0] = int32_t(x0);
            push.src_offset[1] = int32_t(y0);
            push.tscale = t->sx | t->sy << 16;
            break;
        }
        if (source) {
            uint32_t src = tpsm == GSN_PSMCT24 ? SRC_TARGET_CT24 : psm_is_16(tpsm) ? SRC_TARGET_CT16 : SRC_TARGET_CT32;
            push.flags |= src << F_SRC_SHIFT;
            reads_target = true;
            if (source == color) {
                reads_own_target = true;
            } else {
                msaa_settle(*source);
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
                tex_alpha_max = 0;
                for (uint32_t i = 0; i < 256; i++) {
                    tex_alpha_max = std::max(tex_alpha_max, clut[i] >> 24);
                }
            }
            if (cutout_aa_mode != GSN_CUTOUT_AA_OFF && target_samples > 1 && (src == SRC_CLUT || src == SRC_CT32)) {
                // the least and most alpha of the indices (CLUT) or alphas (32-bit) its texels have,
                // and how many texels have the least
                cut_texels = true;
                for (uint32_t i = 0; i < 256; i++) {
                    if (tex->texels[i]) {
                        uint32_t a = src == SRC_CLUT ? clut[i] >> 24 : i;
                        cut_lo = std::min(cut_lo, a);
                        cut_hi = std::max(cut_hi, a);
                    }
                }
                for (uint32_t i = 0; i < 256; i++) {
                    cut_all += tex->texels[i];
                    cut_clear += (src == SRC_CLUT ? clut[i] >> 24 : i) == cut_lo ? tex->texels[i] : 0;
                }
            }
        }
    }

    // blending: Vulkan's where it can, the shader's where it cannot or the draw reads its own target
    uint32_t ba = bits(alpha, 0, 2), bb = bits(alpha, 2, 2), bc = bits(alpha, 4, 2), bd = bits(alpha, 6, 2);
    uint32_t fix = bits(alpha, 32, 8);
    FixedBlend blend = {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0};
    bool shader_blend = false;
    bool partial_mask = false;
    for (int i = 0; i < 4; i++) {
        uint32_t m = (fbmsk >> (i * 8)) & 0xFF;
        if (m != 0 && m != 0xFF && !(dst16 && (m == 0x07 || m == 0x7F))) {
            partial_mask = true;
        }
    }
    // Vulkan's blending rounds where the GS truncates: exact for a layer added or taken away, and when
    // only one of Cs and Cd is kept, but not for (Cs - Cd) C + Cd. That is a difference of 1 a draw,
    // which the passes over the whole frame (the dimming each frame, the previous frame blended in, the
    // smear) carry on frame after frame: sprites, and draws whose texture is a render target, blend
    // here, exactly. (3D geometry is drawn again each frame, and keeps Vulkan's blending.)
    bool fixed = fixed_blend(ba, bb, bc, bd, fix, &blend);
    bool exact = fixed && (blend.premul != 0 || ba == bb);
    bool sprite = gs->batch_sprites;
    // --native-cutout-aa: a cut-out is a draw whose texture is see-through in places and solid in
    // others: a quarter or more of its texels have its least alpha (a mask, not a layer blended by a
    // smooth alpha as the walls' are, or one see-through all over as the mist's), which the alpha test
    // throws away where others pass (from the alphas its vertices and texels can make: a filtered
    // texel's alpha is between its four's, a primitive's between its vertices'), or which is 0 and
    // blended by. The netting, foliage, fences and grates are both: alpha > 0 drawn, blended by As.
    // Their edges inside the primitives are where the texture's alpha falls, which a pixel shaded once
    // sees at one point; each sample shaded on its own sees the texture, and is tested and blended, at
    // its own place (the AFAIL modes as exact as before, a sample at a time). Not the 2D, which covers
    // whole pixels as without MSAA, nor the frame's passes, whose textures are targets.
    bool cutout = false;
    if (cut_texels && bits(tex0, 34, 1) && cut_lo < cut_hi && cut_clear * 4 >= cut_all && !sprite && !fst &&
        cutout_aa_mode == GSN_CUTOUT_AA_SSAA && sample_shading) {
        uint32_t tfx = bits(tex0, 35, 2);
        uint32_t lo = tfx == 0 ? (as_min * cut_lo) >> 7 : tfx == 2 ? std::min(255u, as_min + cut_lo) : cut_lo;
        uint32_t hi = tfx == 0 ? std::min(255u, (as_max * cut_hi) >> 7) : tfx == 2 ? std::min(255u, as_max + cut_hi) : cut_hi;
        uint32_t ref = bits(test, 4, 8);
        bool splits = false;
        if (ate) {
            switch (atst) {
            case 2: // LESS
            case 5: // GEQUAL
                splits = lo < ref && hi >= ref;
                break;
            case 3: // LEQUAL
            case 6: // GREATER
                splits = lo <= ref && hi > ref;
                break;
            case 4: // EQUAL
            case 7: // NOTEQUAL
                splits = lo <= ref && ref <= hi && lo < hi;
                break;
            }
        }
        cutout = splits || (abe && bc == 0 && ba != bb && lo == 0 && hi > 0);
    }
    // Blending by an As above 0x80 (2.0): Vulkan's blend factor (and the alpha out that stands for it)
    // stops at 1.0. The largest As the batch can make, from its vertices and what is known of the
    // texture's alpha (a CLUT's entries, TEXA for 24 and 16-bit texels):
    if (tme && bits(tex0, 34, 1)) { // TCC
        uint32_t tpsm = bits(tex0, 20, 6);
        if (tpsm == GSN_PSMCT24) {
            tex_alpha_max = bits(gs->reg[R_TEXA], 0, 8);
        } else if (psm_is_16(tpsm)) {
            tex_alpha_max = std::max(bits(gs->reg[R_TEXA], 0, 8), bits(gs->reg[R_TEXA], 32, 8));
        }
        uint32_t tfx = bits(tex0, 35, 2);
        as_max = tfx == 0 ? std::min(255u, (as_max * tex_alpha_max) >> 7)
                 : tfx == 2 ? std::min(255u, as_max + tex_alpha_max)
                            : tex_alpha_max;
    }
    bool as_over = bc == 0 && !blend.premul && as_max > 0x80;
    if (date || partial_mask || (abe && (reads_own_target || ((reads_target || sprite) && !exact) || dst16 ||
                                         (push.misc & (M_PABE)) || !(push.misc & M_COLCLAMP) ||
                                         (bc == 0 && (push.misc & M_FBA)) || as_over || !fixed))) {
        shader_blend = true;
    }
    if (shader_blend) {
        push.flags |= F_SHADER_BLEND | (date ? F_DATE : 0);
        if (!abe) {
            push.alpha = (2 << 0) | (2 << 2) | (2 << 4) | (0 << 6); // (0 - 0) * FIX + Cs: no blending
        }
        gsn_stats.shader_blends++;
    } else if (abe && blend.premul) {
        push.flags |= blend.premul;
    } else if (abe && bc == 0 && !(ba == bb)) {
        push.flags |= F_ALPHA_FACTOR;
    }
    gsn_stats.ns_draw_setup += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - timer.start).count());
    Timer record(gsn_stats.ns_draw_record);
    uint32_t scale_x = color->sx, scale_y = color->sy;
    uint32_t x1 = std::min(sx1 + 1, color->width), y1 = std::min(sy1 + 1, color->height);
    if (sx0 >= x1 || sy0 >= y1) {
        gs->verts.clear();
        return;
    }
    // Triangles wholly outside the scissor draw nothing: left out, before the runs below are cut (a
    // reflection drawn into its 256 x 256 target from the whole scene had most of its triangles off it,
    // each a run of its own). (A pixel's margin each way.)
    {
        float cx0 = float(sx0) - 1.0f, cy0 = float(sy0) - 1.0f, cx1 = float(x1) + 1.0f, cy1 = float(y1) + 1.0f;
        size_t kept = 0, n = gs->verts.size();
        Vtx *v = gs->verts.data();
        for (size_t i = 0; i + 2 < n; i += 3) {
            float tx0 = std::min(v[i].x, std::min(v[i + 1].x, v[i + 2].x));
            float tx1 = std::max(v[i].x, std::max(v[i + 1].x, v[i + 2].x));
            float ty0 = std::min(v[i].y, std::min(v[i + 1].y, v[i + 2].y));
            float ty1 = std::max(v[i].y, std::max(v[i + 1].y, v[i + 2].y));
            if (tx1 < cx0 || tx0 > cx1 || ty1 < cy0 || ty0 > cy1) {
                continue;
            }
            if (kept != i) {
                v[kept] = v[i];
                v[kept + 1] = v[i + 1];
                v[kept + 2] = v[i + 2];
            }
            kept += 3;
        }
        gsn_stats.primitives += (n - kept) / 3;
        gs->verts.resize(kept);
        if (kept == 0) {
            gs->batch_sprites = false;
            return;
        }
    }
    push.misc |= scale_y << M_SCALE_SHIFT | scale_x << M_SCALE_X_SHIFT; // (SCANMSK's lines; nearest reads, gs_frag.h)
    // Each pixel's Z as the GS has it: the vertices' Z interpolated, rounded to a whole Z and kept
    // within the Z buffer's format (gs_zclamp.frag writes the depth). The GPU's own interpolation keeps
    // the fraction, so of two draws over the same plane (the lava's glow over the lava, ZTST GEQUAL)
    // the second could come out a hair behind the first where the GS has them equal; and clamping a
    // vertex beyond the format would tilt the plane.
    bool zclamp = depth != nullptr;
    if (zclamp) {
        push.misc |= M_ZCLAMP;
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
            // (a 16-bit frame buffer's alpha bit is written all the same, as paraLLEl-GS has it)
            fail_mask = dst16 ? write_mask : write_mask & 7;
            break;
        }
        if (fail_mask || fail_z) {
            passes[npasses++] = {atst == 0 ? 0u : 2u, fail_mask, fail_z};
        }
    }

    // A draw that reads the target it draws into (shader blending, DATE) reads a copy taken before it,
    // and one drawn in two passes (the alpha test's) draws every passing pixel before any failing one.
    // Either way, primitives of the batch that overlap would not see each other as the GS's do, one
    // after another: such a batch is drawn in runs of primitives that do not overlap, each with its
    // own copy and passes. (A draw whose texture is its own target reads one copy, taken before the
    // batch, for both.)
    // --native-msaa: the draws with Z to the multisampled images, and those without while they are newer
    // (unless they read their own target, which is resolved for it); the rest to the image
    bool ms = target_samples > 1 && (depth || (color->ms_newer && !reads_own_target));
    if (ms) {
        msaa_ready(*color);
        if (depth) {
            msaa_ready(*depth);
        }
        // (from here on: each run's copy of the target, below, is resolved from it)
        color->ms_newer = true;
        if (depth && zwrite) {
            depth->ms_newer = true;
        }
        // (the 2D, rectangles of one colour and Z, is shaded once a pixel: --native-cutout-aa)
        if (flat_2d(gs->verts)) {
            cutout = false;
        }
    } else {
        msaa_settle(*color);
        if (scale_x > 1 || scale_y > 1) {
            flat_2d(gs->verts);
        }
    }

    std::vector<uint32_t> runs(1, 0);
    if ((shader_blend || npasses > 1) && !reads_own_target) {
        uint32_t triangles = uint32_t(gs->verts.size() / 3), start = 0;
        for (uint32_t t = 1; t < triangles; t++) {
            // (a long run is cut anyway: the test is against each triangle of the run)
            bool cut = t - start >= 256;
            for (uint32_t u = start; u < t && !cut; u++) {
                cut = triangles_overlap(&gs->verts[t * 3], &gs->verts[u * 3]);
            }
            if (cut) {
                runs.push_back(t * 3);
                start = t;
            }
        }
    }
    runs.push_back(uint32_t(gs->verts.size()));

    for (size_t r = 0; r + 1 < runs.size(); r++) {
        uint32_t first = runs[r], count = runs[r + 1] - runs[r];
        const Image *dest_copy = nullptr;
        if (shader_blend || reads_own_target) {
            // a draw that reads its own target reads anywhere in it; one that blends, only its own pixels
            int32_t box[4] = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
            for (uint32_t i = first; i < first + count; i++) {
                const Vtx &v = gs->verts[i];
                box[0] = std::min(box[0], int32_t(floorf(v.x)) - 1);
                box[1] = std::min(box[1], int32_t(floorf(v.y)) - 1);
                box[2] = std::max(box[2], int32_t(ceilf(v.x)) + 2);
                box[3] = std::max(box[3], int32_t(ceilf(v.y)) + 2);
            }
            dest_copy = &snapshot_of(*color, reads_own_target ? nullptr : box);
            if (reads_own_target) {
                target_view = &dest_copy->get_view();
            }
        }

        begin_pass(color, depth, ms);
        CommandBuffer &c = cmd();
        c.set_opaque_state();
        // (the cut-outs at every sample: draws with Z into the multisampled images)
        bool per_sample = cutout && ms && zclamp;
        c.set_program(per_sample ? gs->program_zclamp_sample : zclamp ? gs->program_zclamp : gs->program);
        if (per_sample && r == 0) {
            gsn_stats.cutout_draws++;
        }
        c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        c.set_cull_mode(VK_CULL_MODE_NONE);
        VkViewport vp = {0.0f, 0.0f, float(color->width * scale_x), float(color->height * scale_y), 0.0f, 1.0f};
        c.set_viewport(vp);
        c.set_scissor({{int32_t(sx0 * scale_x), int32_t(sy0 * scale_y)}, {(x1 - sx0) * scale_x, (y1 - sy0) * scale_y}});
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

        // the vertices
        size_t bytes = size_t(count) * sizeof(Vtx);
        void *vdata = c.allocate_vertex_data(0, bytes, sizeof(Vtx));
        memcpy(vdata, gs->verts.data() + first, bytes);
        c.set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vtx, x));
        c.set_vertex_attrib(1, 0, VK_FORMAT_R32_UINT, offsetof(Vtx, z));
        c.set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Vtx, rgba));
        c.set_vertex_attrib(3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vtx, s));
        c.set_vertex_attrib(4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vtx, fog));

        for (int i = 0; i < npasses; i++) {
            Push p = push;
            p.flags |= passes[i].select << F_PASS_SHIFT;
            c.set_color_write_mask(passes[i].mask);
            if (depth) {
                c.set_depth_test(true, passes[i].zwrite);
            }
            c.push_constants(&p, 0, sizeof(p));
            c.draw(count);
            gsn_stats.draws++;
        }
    }
    // the rows the batch can have drawn into (its vertices' within the scissor, a row more each way)
    uint32_t dirty0 = uint32_t(std::max(float(sy0), std::max(0.0f, floorf(vy0) - 1.0f)));
    uint32_t dirty1 = uint32_t(std::min(float(y1), std::max(0.0f, ceilf(vy1) + 2.0f)));
    mark_dirty(*color, dirty0, dirty1);
    if (depth && zwrite) {
        mark_dirty(*depth, dirty0, dirty1);
    }
    if (!ms) {
        msaa_image_written(*color);
    }
    gsn_stats.primitives += gs->verts.size() / 3;
    gs->verts.clear();
    gs->batch_sprites = false;
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

// A primitive drawn with SCANMSK into one field's lines and over more than 400 of them (Y within the
// scissor): the game draws its two fields as two pictures (Game_ShiftScreenByViewTurn puts the frame
// before, moved by the turn, on the even lines), so the next vblanks' pictures are read a field at a
// time, as the PS2 shows them ("The scanout"; paraLLEl-GS takes the same sign)
void scanmsk_seen(float y0, float y1) {
    if (!(gs->reg[R_SCANMSK] & 2)) {
        return;
    }
    uint64_t scissor = gs->reg[R_SCISSOR_1 + ((gs->batch_prim >> 9) & 1)];
    float top = std::max(y0, float(bits(scissor, 32, 11))), bottom = std::min(y1, float(bits(scissor, 48, 11)));
    if (bottom - top > 400.0f) {
        GSN_LOG(3, "SCANMSK %u primitive over lines %.1f-%.1f\n", uint32_t(gs->reg[R_SCANMSK] & 3), top, bottom);
        gs->field_scan_vblanks = 4;
    }
}

void add_triangle(const Vtx &a, const Vtx &b, const Vtx &c) {
    scanmsk_seen(std::min({a.y, b.y, c.y}), std::max({a.y, b.y, c.y}));
    bool gouraud = (gs->batch_prim >> 3) & 1;
    gs->verts.push_back(a);
    gs->verts.push_back(b);
    gs->verts.push_back(c);
    if (!gouraud) {
        // flat: the last vertex's colour
        Vtx *v = &gs->verts[gs->verts.size() - 3];
        v[0].rgba = v[1].rgba = c.rgba;
    }
}

// A UV sprite filtered bilinearly whose texture coordinates run with its position (a texel a pixel,
// within a sixteenth) and land on texels' middles, or a sixteenth past them on a size not a power of
// two: the GS's DDA steps a hair short and never leaves the middle, so the filter has nothing to
// blend (paraLLEl-GS's set-up turns such a sprite's filter to nearest). Its UV is put on the middles
// exactly, where bilinear filtering is nearest. (Signed: a corner's UV can come out under 0.)
bool snap_sprite_uv(const QVertex &a, const QVertex &b, uint32_t prim, int32_t *snapped_a, int32_t *snapped_b) {
    uint32_t ctxt = (prim >> 9) & 1;
    if (!((prim >> 8) & 1) || !bits(gs->reg[R_TEX1_1 + ctxt], 5, 1)) {
        return false; // STQ, or nearest already
    }
    uint64_t offset = gs->reg[R_XYOFFSET_1 + ctxt];
    int32_t pa[2] = {int32_t(a.x) - int32_t(bits(offset, 0, 16)), int32_t(a.y) - int32_t(bits(offset, 32, 16))};
    int32_t pb[2] = {int32_t(b.x) - int32_t(bits(offset, 0, 16)), int32_t(b.y) - int32_t(bits(offset, 32, 16))};
    int32_t ua[2] = {int32_t(a.u), int32_t(a.v)}, ub[2] = {int32_t(b.u), int32_t(b.v)};
    int32_t lo[2];
    for (int i = 0; i < 2; i++) {
        int32_t d0 = ua[i] - pa[i], d1 = ub[i] - pb[i];
        int32_t mn = std::min(d0, d1), mx = std::max(d0, d1);
        int32_t dist = std::abs(pb[i] - pa[i]);
        bool npot = (dist & (dist - 1)) != 0;
        bool centre = (mn & 15) == 8, next = (mx & 15) == 9 && npot;
        if (!(centre || next) || mx - mn > 1) {
            return false;
        }
        lo[i] = (mn & ~15) | 8; // the middle
    }
    for (int i = 0; i < 2; i++) {
        snapped_a[i] = pa[i] + lo[i];
        snapped_b[i] = pb[i] + lo[i];
    }
    return true;
}

void add_sprite(const QVertex &a, const QVertex &b) {
    uint32_t prim = gs->batch_prim;
    gs->batch_sprites = true;
    int32_t ua[2], ub[2];
    bool snapped = snap_sprite_uv(a, b, prim, ua, ub);
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
    if (snapped) {
        // corners 0 and 2 have a's U, 0 and 1 a's V
        for (int i = 0; i < 4; i++) {
            v[i].s = float(i == 0 || i == 2 ? ua[0] : ub[0]) / 16.0f;
            v[i].t = float(i == 0 || i == 1 ? ua[1] : ub[1]) / 16.0f;
        }
    }
    scanmsk_seen(std::min(v[0].y, v[3].y), std::max(v[0].y, v[3].y));
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
        gs->batch_sprites = false;
    }
    uint32_t type = prim & 7;
    QVertex *q = gs->queue;
    Vtx *qv = gs->queued;
    switch (type) {
    case 3: // triangles
        qv[gs->queue_count] = make_vtx(v, prim, v);
        q[gs->queue_count++] = v;
        if (gs->queue_count == 3) {
            if (draw) {
                add_triangle(qv[0], qv[1], qv[2]);
            }
            gs->queue_count = 0;
        }
        break;
    case 4: // a strip
        if (gs->queue_count == 3) {
            q[0] = q[1];
            q[1] = q[2];
            qv[0] = qv[1];
            qv[1] = qv[2];
            gs->queue_count = 2;
        }
        qv[gs->queue_count] = make_vtx(v, prim, v);
        q[gs->queue_count++] = v;
        if (gs->queue_count == 3 && draw) {
            add_triangle(qv[0], qv[1], qv[2]);
        }
        break;
    case 5: // a fan
        if (gs->queue_count == 3) {
            q[1] = q[2];
            qv[1] = qv[2];
            gs->queue_count = 2;
        }
        qv[gs->queue_count] = make_vtx(v, prim, v);
        q[gs->queue_count++] = v;
        if (gs->queue_count == 3 && draw) {
            add_triangle(qv[0], qv[1], qv[2]);
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

std::vector<uint32_t> whole_pages(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, bool *rgb_only = nullptr);

// The rectangle has just been written in the CPU's VRAM: the targets over it, the same shape as it
// (row_alias: perhaps starting some rows apart), take it too; those in another shape that were kept
// (prepare_write: it writes whole pages of them) take those pages again; the others were written back
// and dropped before
void sync_targets(const TransferRect &r) {
    std::vector<uint32_t> whole = whole_pages(gs->reg[R_BITBLTBUF], gs->reg[R_TRXPOS], gs->reg[R_TRXREG]);
    for (auto &t : gs->targets) {
        int32_t rows;
        if (!row_alias(*t, r.bp, r.bw, r.psm, &rows)) {
            if (!regular(*t)) {
                continue;
            }
            uint32_t tlo, tcount;
            target_pages(*t, &tlo, &tcount);
            uint32_t ph = page_height(t->psm);
            for (uint32_t page : whole) {
                uint32_t k = (page + vram_pages - tlo) % vram_pages;
                if (k < tcount) {
                    load_target_rect(*t, (k % t->bw) * 64, (k / t->bw) * ph, 64, ph);
                }
            }
        } else {
            int32_t y = int32_t(r.y) + rows;
            uint32_t h = r.h;
            if (y < 0) {
                if (uint32_t(-y) >= h) {
                    continue;
                }
                h -= uint32_t(-y);
                y = 0;
            }
            load_target_rect(*t, r.x, uint32_t(y), r.w, h);
        }
    }
}


// Before the rectangle is written in the CPU's VRAM: the targets over it in another shape go back to
// VRAM and stop being targets (unless it writes whole pages of them, or all of them)
void prepare_write(const TransferRect &r) {
    uint32_t lo, count;
    rect_pages(r, &lo, &count);
    // the pages the transfer writes whole (when it is an upload of whole pages): a target entirely
    // under them loses every pixel, and needs no writing back first
    std::vector<uint32_t> whole = whole_pages(gs->reg[R_BITBLTBUF], gs->reg[R_TRXPOS], gs->reg[R_TRXREG]);
    std::sort(whole.begin(), whole.end());
    for (size_t i = 0; i < gs->targets.size();) {
        Target *t = gs->targets[i].get();
        uint32_t tlo, tcount;
        target_pages(*t, &tlo, &tcount);
        int32_t rows;
        bool same = row_alias(*t, r.bp, r.bw, r.psm, &rows);
        if (pages_overlap(lo, count, tlo, tcount)) {
            bool covered = !whole.empty() && t->bp % 32 == 0;
            for (uint32_t k = 0; covered && k < tcount; k++) {
                covered = std::binary_search(whole.begin(), whole.end(), (tlo + k) % vram_pages);
            }
            if (same && (rows == 0 || !covered)) {
                // the rest of the target stays newer than VRAM; the rectangle, after the write, both
                // (sync_targets). (A target some rows from the transfer that it covers whole is let go.)
                i++;
                continue;
            }
            // A target whose pages under the transfer are all written whole keeps the rest, and takes
            // those pages from VRAM after the write (sync_targets): it need not go back to VRAM first
            // (not a Z buffer: one may share its pages with another, take_pages)
            bool keep = !covered && !whole.empty() && regular(*t) && !t->depth;
            for (uint32_t k = 0; keep && k < tcount; k++) {
                uint32_t page = (tlo + k) % vram_pages;
                if (pages_overlap(lo, count, page, 1)) {
                    keep = std::binary_search(whole.begin(), whole.end(), page);
                }
            }
            if (keep) {
                i++;
                continue;
            }
            if (!covered) {
                write_back(*t);
            }
            drop_target(t);
            continue;
        }
        i++;
    }
}

// Uploads (host to local transfers). The game sends several MB a frame, much of it the same textures
// to the same places again (they take turns in the VRAM it shares between them), and the swizzle into
// the GS's layout is the native GS's largest cost. So an upload's bytes are kept until all have come,
// then written at once; one that fills whole pages leaves its pages' bytes behind, found again by a
// hash of the upload, and an upload found again is a copy of those pages (or nothing, when VRAM
// already holds them). Anything that needs VRAM before an upload is whole has it written so far
// (settle_upload), and the rest then goes in as it comes.
struct PendingUpload {
    bool active;
    bool incremental; // being written as it comes (it was settled before it was whole)
    uint64_t bitbltbuf, trxpos, trxreg;
    size_t expect; // its bytes, whole
    std::vector<uint8_t> data;
};
PendingUpload pending_upload;

struct PageImage {
    std::vector<uint32_t> pages;
    std::vector<uint8_t> bytes; // 8 KB a page, in pages' order
    bool rgb_only;              // a 24-bit upload: the pages' words' low three bytes only
};
std::unordered_map<uint64_t, PageImage> page_images;
std::deque<uint64_t> page_image_order;
size_t page_image_bytes;

// The pages an upload fills entirely, in order; empty if it does not fill whole pages (a format that
// owns only part of each word, or a rectangle not on page bounds). With rgb_only, a 24-bit upload
// counts as filling its pages too (and *rgb_only says it is one).
std::vector<uint32_t> whole_pages(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, bool *rgb_only) {
    std::vector<uint32_t> pages;
    uint32_t dbp = bits(bitbltbuf, 32, 14), dbw = bits(bitbltbuf, 48, 6), psm = bits(bitbltbuf, 56, 6);
    uint32_t x = bits(trxpos, 32, 11), y = bits(trxpos, 48, 11), w = bits(trxreg, 0, 12), h = bits(trxreg, 32, 12);
    bool full = psm == GSN_PSMCT32 || psm == GSN_PSMCT16 || psm == GSN_PSMCT16S || psm == GSN_PSMT8 ||
                psm == GSN_PSMT4 || psm == GSN_PSMZ32 || psm == GSN_PSMZ16 || psm == GSN_PSMZ16S;
    if (rgb_only) {
        *rgb_only = psm == GSN_PSMCT24 || psm == GSN_PSMZ24;
        full = full || *rgb_only;
    }
    uint32_t pw = page_width(psm), ph = page_height(psm);
    if (psm == GSN_PSMZ16 || psm == GSN_PSMZ16S) {
        ph = 64;
    }
    uint32_t across = (psm == GSN_PSMT8 || psm == GSN_PSMT4) ? dbw / 2 : dbw;
    if (!full || dbp % 32 || x % pw || y % ph || w % pw || h % ph || w == 0 || h == 0 || across == 0 ||
        x + w > across * pw || x + w > 2048 || y + h > 2048) {
        return pages;
    }
    for (uint32_t r = y / ph; r < (y + h) / ph; r++) {
        for (uint32_t c = x / pw; c < (x + w) / pw; c++) {
            pages.push_back((dbp / 32 + r * across + c) % vram_pages);
        }
    }
    return pages;
}

inline uint64_t rotate_left(uint64_t v, int n) {
    return (v << n) | (v >> (64 - n));
}

// An upload's bytes, and where they go, as 64 bits. (Four lanes of 8 bytes each, so that the
// multiplies do not wait for each other: it runs at about the speed of reading the bytes.)
uint64_t upload_hash(const PendingUpload &p, const uint8_t *d, size_t size) {
    const uint64_t K1 = 0x9E3779B97F4A7C15ull, K2 = 0xC2B2AE3D27D4EB4Full, K3 = 0x165667B19E3779F9ull;
    uint64_t lane[4] = {p.bitbltbuf ^ K1, p.trxpos ^ K2, p.trxreg ^ K3, size};
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        for (int k = 0; k < 4; k++) {
            uint64_t v;
            memcpy(&v, d + i + k * 8, 8);
            lane[k] = rotate_left(lane[k] + v * K2, 31) * K1;
        }
    }
    uint64_t h = rotate_left(lane[0], 1) + rotate_left(lane[1], 7) + rotate_left(lane[2], 12) + rotate_left(lane[3], 18);
    for (; i < size; i++) {
        h = (h ^ d[i]) * K3;
    }
    h ^= h >> 33;
    h *= K2;
    h ^= h >> 29;
    h *= K3;
    return h ^ (h >> 32);
}

// An upload's bytes, swizzled into VRAM as they are
void swizzle_upload(const uint8_t *data, size_t bytes) {
    Timer swizzle(gsn_stats.ns_swizzle);
    uint32_t lo = 0, hi = 0;
    if (gsn_upload_data(&gs->upload, gs->vram.data(), data, bytes, &lo, &hi) && lo <= hi) {
        pages_written(lo, hi - lo + 1);
    }
}

void upload_done() {
    {
        Timer sync(gsn_stats.ns_sync);
        sync_targets(destination_rect());
    }
    gs->transfer_dir = 3;
    gs->upload.active = 0;
    pending_upload.active = false;
    pending_upload.data.clear();
}

// The whole upload: its pages copied in if it was seen before, else swizzled (and kept)
void complete_upload(const uint8_t *data, size_t size) {
    PendingUpload &p = pending_upload;
    bool rgb_only = false;
    std::vector<uint32_t> pages = whole_pages(p.bitbltbuf, p.trxpos, p.trxreg, &rgb_only);
    uint64_t key = upload_hash(p, data, size);
    auto found = pages.empty() ? page_images.end() : page_images.find(key);
    if (found != page_images.end() && found->second.pages == pages && found->second.rgb_only == rgb_only) {
        bool changed = false;
        for (size_t i = 0; i < pages.size(); i++) {
            uint8_t *dst = gs->vram.data() + size_t(pages[i]) * 8192;
            const uint8_t *src = found->second.bytes.data() + i * 8192;
            if (rgb_only) {
                // (the words' top bytes are not the upload's: they stay)
                bool page_changed = false;
                for (size_t k = 0; k < 8192; k += 4) {
                    uint32_t d, s;
                    memcpy(&d, dst + k, 4);
                    memcpy(&s, src + k, 4);
                    uint32_t v = (d & 0xFF000000u) | (s & 0xFFFFFFu);
                    page_changed |= v != d;
                    memcpy(dst + k, &v, 4);
                }
                if (page_changed) {
                    pages_written(pages[i], 1);
                    changed = true;
                }
            } else if (memcmp(dst, src, 8192) != 0) {
                memcpy(dst, src, 8192);
                pages_written(pages[i], 1);
                changed = true;
            }
        }
        gsn_stats.uploads_skipped += !changed;
        gsn_stats.uploads_copied += changed;
        upload_done();
        return;
    }
    swizzle_upload(data, size);
    if (!pages.empty()) {
        PageImage image;
        image.pages = pages;
        image.rgb_only = rgb_only;
        image.bytes.resize(pages.size() * 8192);
        for (size_t i = 0; i < pages.size(); i++) {
            memcpy(image.bytes.data() + i * 8192, gs->vram.data() + size_t(pages[i]) * 8192, 8192);
        }
        page_image_bytes += image.bytes.size();
        page_images[key] = std::move(image);
        page_image_order.push_back(key);
        // the oldest let go beyond 64 MB of them
        while (page_image_bytes > (64u << 20) && !page_image_order.empty()) {
            auto old = page_images.find(page_image_order.front());
            if (old != page_images.end()) {
                page_image_bytes -= old->second.bytes.size();
                page_images.erase(old);
            }
            page_image_order.pop_front();
        }
    }
    upload_done();
}

// Something needs VRAM before the upload under way is whole: what has come goes in now
void settle_upload() {
    PendingUpload &p = pending_upload;
    if (!p.active || p.incremental) {
        return;
    }
    p.incremental = true;
    // A target in another shape that prepare_write kept, as the upload writes whole pages of it: those
    // pages are VRAM's from now on (half written), and must not go back over it before sync_targets
    // takes them from it
    std::vector<uint32_t> whole = whole_pages(p.bitbltbuf, p.trxpos, p.trxreg);
    TransferRect r = destination_rect();
    for (auto &t : gs->targets) {
        int32_t rows;
        if (row_alias(*t, r.bp, r.bw, r.psm, &rows) || !regular(*t)) {
            continue;
        }
        uint32_t tlo, tcount;
        target_pages(*t, &tlo, &tcount);
        for (uint32_t page : whole) {
            uint32_t k = (page + vram_pages - tlo) % vram_pages;
            if (k < tcount && k < t->dirty_pages.size()) {
                t->dirty_pages[k] = 0;
            }
        }
    }
    swizzle_upload(p.data.data(), p.data.size());
    p.data.clear();
}

void forget_uploads() {
    pending_upload.active = false;
    pending_upload.data.clear();
    page_images.clear();
    page_image_order.clear();
    page_image_bytes = 0;
}

void begin_transfer(uint32_t dir) {
    settle_upload();
    uint64_t bitbltbuf = gs->reg[R_BITBLTBUF], trxpos = gs->reg[R_TRXPOS], trxreg = gs->reg[R_TRXREG];
    gs->transfer_dir = dir;
    if (dir == 0) {
        TransferRect r = destination_rect();
        prepare_write(r);
        gsn_upload_begin(&gs->upload, bitbltbuf, trxpos, trxreg);
        gsn_stats.uploads++;
        PendingUpload &u = pending_upload;
        u.active = gs->upload.active != 0;
        u.incremental = false;
        u.bitbltbuf = bitbltbuf;
        u.trxpos = trxpos;
        u.trxreg = trxreg;
        u.expect = (size_t(bits(trxreg, 0, 12)) * bits(trxreg, 32, 12) * gsn_psm_bits(bits(bitbltbuf, 56, 6)) + 127) / 128 * 16;
        u.data.clear();
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
                msaa_settle(*src);
                msaa_settle(*dst);
                end_pass();
                transfer_barrier();
                uint32_t kx = src->sx, ky = src->sy;
                cmd().copy_image(*dst->image, *src->image, {int32_t(d.x * kx), int32_t(d.y * ky), 0},
                                 {int32_t(sx * kx), int32_t(sy * ky), 0}, {w * kx, h * ky, 1},
                                 {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
                transfer_barrier();
                mark_dirty(*dst, d.y, d.y + h);
                msaa_image_written(*dst);
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
    Timer timer(gsn_stats.ns_upload);
    if (gs->transfer_dir != 0 || !gs->upload.active) {
        return;
    }
    gsn_stats.upload_bytes += bytes;
    PendingUpload &p = pending_upload;
    if (p.incremental) {
        swizzle_upload(data, bytes);
        if (!gs->upload.active) {
            upload_done();
        }
        return;
    }
    if (p.data.empty() && bytes >= p.expect) {
        complete_upload(data, bytes); // (all of it at once: no need to keep it)
        return;
    }
    p.data.insert(p.data.end(), data, data + bytes);
    if (p.data.size() >= p.expect) {
        complete_upload(p.data.data(), p.data.size());
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
        settle_upload(); // the CLUT comes from VRAM
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
    case 0x7D: // the port's frame-rate mark (frame_interp.c)
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
// (scaled: as many pixels as the target has, its scale across and down returned in got_scale[0] and
// [1]; else one a GS pixel)
void read_buffer(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 uint32_t *out, bool scaled = false, uint32_t *got_scale = nullptr) {
    Target *t = find_target(bp, bw, psm);
    if (got_scale) {
        got_scale[0] = got_scale[1] = 1;
    }
    if (t && x + w <= t->width && y + h <= t->height) {
        msaa_settle(*t);
        uint32_t sx = t->sx, sy = t->sy;
        const uint8_t *pixels = read_image(*t->image, x * sx, y * sy, w * sx, h * sy);
        if (scaled) {
            for (size_t i = 0; i < size_t(w) * h * sx * sy; i++) {
                uint32_t v;
                memcpy(&v, pixels + i * 4, 4);
                out[i] = v | 0xFF000000u;
            }
            if (got_scale) {
                got_scale[0] = sx;
                got_scale[1] = sy;
            }
        } else {
            for (uint32_t j = 0; j < h; j++) {
                for (uint32_t i = 0; i < w; i++) {
                    uint32_t v;
                    memcpy(&v, pixels + (size_t(j * sy + sy / 2) * w * sx + i * sx + sx / 2) * 4, 4);
                    out[size_t(j) * w + i] = v | 0xFF000000u;
                }
            }
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
    Shader *fs_zclamp = gs->device->request_shader(gsn_spv_gs_zclamp_frag, sizeof(gsn_spv_gs_zclamp_frag), &frag);
    gs->program_zclamp = gs->device->request_program(vs, fs_zclamp);
    if (sample_shading) {
        Shader *fs_sample = gs->device->request_shader(gsn_spv_gs_zclamp_sample_frag, sizeof(gsn_spv_gs_zclamp_sample_frag), &frag);
        gs->program_zclamp_sample = gs->device->request_program(vs, fs_sample);
    }

    // --native-msaa's copying passes: a texture in, a colour or a depth out
    ResourceLayout copy_vert = {};
    ResourceLayout copy_frag = {};
    copy_frag.sets[0].sampled_image_mask = 1u << 0;
    copy_frag.sets[0].fp_mask = 1u << 0;
    copy_frag.sets[0].meta[0].array_size = 1;
    ResourceLayout copy_colour = copy_frag;
    copy_colour.output_mask = 1;
    Shader *copy_vs = gs->device->request_shader(gsn_spv_msaa_vert, sizeof(gsn_spv_msaa_vert), &copy_vert);
    gs->program_ms_load = gs->device->request_program(
        copy_vs, gs->device->request_shader(gsn_spv_msaa_load_frag, sizeof(gsn_spv_msaa_load_frag), &copy_colour));
    gs->program_ms_load_depth = gs->device->request_program(
        copy_vs, gs->device->request_shader(gsn_spv_msaa_load_depth_frag, sizeof(gsn_spv_msaa_load_depth_frag), &copy_frag));
    gs->program_ms_depth = gs->device->request_program(
        copy_vs, gs->device->request_shader(gsn_spv_msaa_depth_frag, sizeof(gsn_spv_msaa_depth_frag), &copy_frag));

    // the scanout's deinterlace: four images in, the picture out
    ResourceLayout deinterlace = {};
    deinterlace.output_mask = 1;
    deinterlace.push_constant_size = 3 * sizeof(int32_t);
    deinterlace.sets[0].sampled_image_mask = 0xF;
    deinterlace.sets[0].fp_mask = 0xF;
    for (int b = 0; b < 4; b++) {
        deinterlace.sets[0].meta[b].array_size = 1;
    }
    gs->program_deinterlace = gs->device->request_program(
        copy_vs, gs->device->request_shader(gsn_spv_deinterlace_frag, sizeof(gsn_spv_deinterlace_frag), &deinterlace));

    // --native-colour high's scanout: a 16-bit target in, an 8-bit picture out
    ResourceLayout narrowing = copy_colour;
    narrowing.push_constant_size = 8 * sizeof(int32_t);
    gs->program_narrow = gs->device->request_program(
        copy_vs, gs->device->request_shader(gsn_spv_narrow_frag, sizeof(gsn_spv_narrow_frag), &narrowing));
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
    gs->field_scan_vblanks = 0;
}

// ---- The scanout as an image (gsn_scanout_take) ---------------------------------------------------
// With a presenter on this device (port/platform/present_vk.cpp) the buffer the display shows is
// copied on the GPU into an image of its own, which the presenter draws from: no read-back. In field
// mode the image is the woven picture, each field's lines copied into its own rows (line l of the
// picture is field (l & 1)'s line l / 2), so the field before stays in the other rows.
struct ScanoutImage {
    std::mutex lock; // (the presenter takes the image on the game's thread)
    ImageHandle image;
    std::vector<ImageHandle> retired; // images replaced while the presenter had one: dropped once it lets go
    uint32_t width = 0, height = 0;
    uint64_t serial = 0;
    bool taken = false;
};
ScanoutImage scanout_image;
bool present_wanted; // gsn_enable_present

// The surface extensions the Vulkan loader has (VK_KHR_surface and each platform's: SDL picks one;
// VK_EXT_headless_surface for SDL's offscreen driver)
void present_extensions(std::vector<const char *> &instance_ext, std::vector<const char *> &device_ext) {
    static const char *const surfaces[] = {"VK_KHR_win32_surface", "VK_KHR_xlib_surface", "VK_KHR_xcb_surface",
                                           "VK_KHR_wayland_surface", "VK_EXT_metal_surface",
                                           "VK_EXT_headless_surface"};
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
        return;
    }
    std::vector<VkExtensionProperties> have(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, have.data());
    auto has = [&](const char *name) {
        for (auto &e : have) {
            if (strcmp(e.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };
    if (!has(VK_KHR_SURFACE_EXTENSION_NAME)) {
        return;
    }
    for (const char *name : surfaces) {
        if (has(name)) {
            instance_ext.push_back(name);
        }
    }
    if (instance_ext.empty()) {
        return;
    }
    instance_ext.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    device_ext.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

// gsn_vsync's want_pixels as vsync_now has it: the PC's switches (port_options) added
enum : int { VSYNC_PROGRESSIVE = 1 << 8, VSYNC_DEINTERLACE = 1 << 9 };

// How the displayed buffer becomes the picture ("The scanout", at vsync_now)
enum ScanMode : uint32_t {
    SCAN_WHOLE,       // every line, as it is (progressive frame mode, a field picture alone)
    SCAN_WEAVE,       // field mode: this field's picture into its own lines of a picture twice as tall
    SCAN_FIELD,       // frame mode read a field at a time, without --deinterlace: this field's lines alone
    SCAN_DEINTERLACE, // the same with --deinterlace: deinterlace.frag over the buffer's last four vblanks
};

// The displayed buffer: where it is, and its target if it has one (and holds the whole of it)
struct Shown {
    uint32_t fbp, fbw, psm, dbx, dby, w, h;
    Target *t;
    uint32_t sx, sy; // its target's scale (or the targets' when it has none)
};

Shown shown_buffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t dbx, uint32_t dby, uint32_t w, uint32_t h) {
    Shown s = {fbp, fbw, psm, dbx, dby, w, h, find_target(fbp, fbw, psm), target_sx, target_sy};
    if (s.t && (s.t->depth || dbx + w > s.t->width || dby + h > s.t->height)) {
        s.t = nullptr;
    }
    if (s.t) {
        s.sx = s.t->sx;
        s.sy = s.t->sy;
    }
    return s;
}

// count of the buffer's lines, from line `first` every `step`, into dst: the m-th into the band of sy
// rows of its line dst_first + dst_step * m. From the target on the GPU, or else from the CPU's VRAM
// (each GS pixel repeated sx times across and sy down).
void copy_lines(const Image &dst, const Shown &s, uint32_t first, uint32_t step, uint32_t count, uint32_t dst_first,
                uint32_t dst_step) {
    const VkImageSubresourceLayers colour = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    uint32_t iw = s.w * s.sx, sy = s.sy;
    if (s.t && s.t->image->get_format() != dst.get_format()) {
        // --native-colour high: the 16-bit target dithered into the 8-bit image (shaders/narrow.frag)
        msaa_settle(*s.t);
        end_pass();
        RenderPassInfo rp;
        rp.num_color_attachments = 1;
        rp.color_attachments[0] = &dst.get_view();
        rp.load_attachments = 1;
        rp.store_attachments = 1;
        CommandBuffer &c = cmd();
        c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        c.begin_render_pass(rp);
        c.set_opaque_state();
        c.set_program(gs->program_narrow);
        c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        c.set_cull_mode(VK_CULL_MODE_NONE);
        c.set_depth_test(false, false);
        c.set_texture(0, 0, s.t->image->get_view(), StockSampler::NearestClamp);
        int32_t push[8] = {int32_t(s.dbx * s.sx), int32_t(s.dby * sy), int32_t(first), int32_t(step),
                           int32_t(dst_first), int32_t(dst_step), int32_t(count), int32_t(sy)};
        c.push_constants(push, 0, sizeof(push));
        c.draw(3);
        c.end_render_pass();
        c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        return;
    }
    if (s.t) {
        msaa_settle(*s.t);
        end_pass();
        transfer_barrier();
        if (step == 1 && dst_step == 1) {
            cmd().copy_image(dst, *s.t->image, {0, int32_t(dst_first * sy), 0},
                             {int32_t(s.dbx * s.sx), int32_t((s.dby + first) * sy), 0}, {iw, count * sy, 1}, colour,
                             colour);
        } else {
            for (uint32_t m = 0; m < count; m++) {
                cmd().copy_image(dst, *s.t->image, {0, int32_t((dst_first + dst_step * m) * sy), 0},
                                 {int32_t(s.dbx * s.sx), int32_t((s.dby + first + step * m) * sy), 0}, {iw, sy, 1},
                                 colour, colour);
            }
        }
    } else {
        std::vector<uint32_t> small(size_t(s.w) * s.h);
        read_buffer(s.fbp, s.fbw, s.psm, s.dbx, s.dby, s.w, s.h, small.data());
        end_pass();
        transfer_barrier();
        for (uint32_t m = 0; m < count; m++) {
            uint32_t line = first + step * m;
            uint32_t *out = static_cast<uint32_t *>(cmd().update_image(
                dst, {0, int32_t((dst_first + dst_step * m) * sy), 0}, {iw, sy, 1}, iw, sy, colour));
            for (uint32_t y = 0; y < sy; y++) {
                for (uint32_t x = 0; x < iw; x++) {
                    out[size_t(y) * iw + x] = small[size_t(line) * s.w + x / s.sx];
                }
            }
        }
    }
    transfer_barrier();
}

// The deinterlace's history let go (a vblank not read a field at a time, or a new size)
void forget_history() {
    for (auto &h : gs->history) {
        h.reset();
    }
    gs->history_filled = 0;
}

// This vblank's field (the buffer's lines of parity `parity`, one under the other) into the
// deinterlace's history (SCAN_DEINTERLACE), the newest first: kept at every vblank read a field at a
// time, a picture wanted or not. (The history is fields as paraLLEl-GS keeps them, which a GS capture
// carries: KF4GS_GS_HISTORY.)
void record_history(const Shown &s, int parity) {
    uint32_t iw = s.w * s.sx, ih = s.h / 2 * s.sy;
    if (gs->history_filled && (gs->history[0]->get_width() != iw || gs->history[0]->get_height() != ih)) {
        forget_history();
    }
    ImageHandle oldest = gs->history_filled == 4 ? gs->history[3] : ImageHandle();
    for (int i = 0; i < 3 && oldest; i++) {
        if (gs->history[i] == oldest) {
            oldest.reset(); // (the same image twice, from a capture: not to be written over)
        }
    }
    for (int i = 3; i > 0; i--) {
        gs->history[i] = gs->history[i - 1];
    }
    gs->history[0] = oldest ? oldest : make_image(iw, ih, VK_FORMAT_R8G8B8A8_UNORM, 1, true);
    gs->history_filled = std::min(gs->history_filled + 1, 4u);
    copy_lines(*gs->history[0], s, uint32_t(parity), 2, s.h / 2, 0, 1);
}

// The picture of the deinterlace into dst (as tall as the buffer), from the history: a field before
// that is not there yet is this one (the vblank three before: the one before), so that with none
// before this field's lines are taken twice
void deinterlace_pass(const Image &dst, int parity, uint32_t sy, uint32_t lines) {
    const Image *before[4];
    for (uint32_t i = 0; i < 4; i++) {
        before[i] = i < gs->history_filled ? gs->history[i].get() : before[i == 3 ? 1 : 0];
    }
    end_pass();
    RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = &dst.get_view();
    rp.store_attachments = 1;
    CommandBuffer &c = cmd();
    c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    c.begin_render_pass(rp);
    c.set_opaque_state();
    c.set_program(gs->program_deinterlace);
    c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    c.set_cull_mode(VK_CULL_MODE_NONE);
    c.set_depth_test(false, false);
    for (uint32_t i = 0; i < 4; i++) {
        c.set_texture(0, i, before[i]->get_view(), StockSampler::NearestClamp);
    }
    int32_t push[3] = {parity, int32_t(sy), int32_t(lines)};
    c.push_constants(push, 0, sizeof(push));
    c.draw(3);
    c.end_render_pass();
    c.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

// The displayed buffer into scanout_image as `mode` says (parity: the lines of this vblank's field; in
// field mode the buffer is the field's picture, and parity the lines of the woven picture it goes
// into); with frame, read back into it too. Returns GSN_WANT_IMAGE, and GSN_WANT_PIXELS when frame has
// the picture. (SCAN_DEINTERLACE draws from the history: record_history first.)
int scanout_to_image(const Shown &s, ScanMode mode, int parity, PortGsFrame *frame) {
    ScanoutImage &so = scanout_image;
    uint32_t lines = mode == SCAN_WEAVE ? s.h * 2 : mode == SCAN_FIELD ? s.h / 2 : s.h;
    uint32_t iw = s.w * s.sx, ih = lines * s.sy;
    bool fresh = false;
    // (images are made and let go outside the lock: Granite's device lock, which the presenter holds
    // while it releases the image (gsn_queue_lock, then gsn_scanout_release), comes before it)
    {
        std::lock_guard<std::mutex> hold(so.lock);
        fresh = !so.image || so.width != iw || so.height != ih;
    }
    ImageHandle made = fresh ? make_image(iw, ih, VK_FORMAT_R8G8B8A8_UNORM, 1, true) : ImageHandle();
    std::vector<ImageHandle> let_go;
    {
        std::lock_guard<std::mutex> hold(so.lock);
        if (!so.taken) {
            let_go.swap(so.retired);
        }
        if (fresh) {
            if (so.taken && so.image) {
                so.retired.push_back(so.image);
            } else if (so.image) {
                let_go.push_back(so.image);
            }
            so.image = made;
            so.width = iw;
            so.height = ih;
        }
    }
    let_go.clear();
    switch (mode) {
    case SCAN_WHOLE:
        copy_lines(*so.image, s, 0, 1, s.h, 0, 1);
        break;
    case SCAN_WEAVE:
        // the field into its own lines; into both when the image is new (as the read-back path starts
        // the other field as this one)
        for (uint32_t p = fresh ? 0 : uint32_t(parity); p <= (fresh ? 1u : uint32_t(parity)); p++) {
            copy_lines(*so.image, s, 0, 1, s.h, p, 2);
        }
        break;
    case SCAN_FIELD:
        copy_lines(*so.image, s, uint32_t(parity), 2, lines, 0, 1);
        break;
    case SCAN_DEINTERLACE:
        deinterlace_pass(*so.image, parity, s.sy, s.h);
        break;
    }
    int ok = GSN_WANT_IMAGE;
    if (frame && size_t(iw) * ih <= PORT_GS_FRAME_PIXELS) {
        const uint8_t *pixels = read_image(*so.image, 0, 0, iw, ih);
        for (size_t i = 0; i < size_t(iw) * ih; i++) {
            uint32_t v;
            memcpy(&v, pixels + i * 4, 4);
            frame->rgba[i] = v | 0xFF000000u;
        }
        done_reading();
        frame->width = iw;
        frame->height = ih;
        ok |= GSN_WANT_PIXELS;
    }
    return ok;
}

// After the vblank's commands went to the queue: the presenter may show the new picture
void scanout_published() {
    std::lock_guard<std::mutex> hold(scanout_image.lock);
    scanout_image.serial++;
}

// ---- The GS's own thread ------------------------------------------------------------------------
// The native GS works on a thread of its own, as the GS is a chip of its own: what is sent to it (GIF
// packets, vblanks) is copied into a ring, and its thread takes it from there while the thread that
// sent it (the DMA controller's) goes on with VU1's programs and the next packets. Whatever wants
// something back (a read-back, a picture to show, the GS's state) first waits until the ring is
// empty and the GS's thread idle, then does it on its own thread. (KF4_GSN_THREAD=0: no thread, all
// of it done as it is sent.)
enum : uint32_t { RING_GIF, RING_VSYNC, RING_SKIP };

// A record in the ring: this header, then its bytes, padded to 16
struct RingRecord {
    uint32_t type;
    uint32_t size;
    int32_t arg;
    uint32_t want_pixels; // (a vblank's)
};
static_assert(sizeof(RingRecord) == 16, "a ring record's header is a qword");

const size_t ring_bytes = size_t(64) << 20; // (a power of two)
// the display registers gsn_vsync reads: PMODE to DISPLAY2, a register every 16 bytes
const size_t vsync_bytes = 11 * 16;

struct Ring {
    std::vector<uint8_t> bytes;
    std::atomic<uint64_t> head{0}; // bytes put in (the sender's)
    std::atomic<uint64_t> tail{0}; // bytes done (the GS thread's)
    std::atomic<bool> gs_sleeping{false}, sender_waiting{false};
    std::atomic<bool> stop{false}, stopped{false}; // (at exit: ring_stop)
    std::atomic<uint64_t> vsyncs_done{0};          // vblanks the GS's thread has done
    uint64_t vsyncs_sent = 0;                      // (the sender's)
    std::mutex lock;
    std::condition_variable wake; // the GS's thread, when there is something to do
    std::condition_variable done; // the sender, when the GS's thread has done something
};

Ring *ring; // (none: no thread)

int vsync_now(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);

void ring_work(const RingRecord &r, const uint8_t *data) {
    if (r.type == RING_GIF) {
        Timer timer(gsn_stats.ns_gif);
        gif(r.arg, data, r.size);
    } else if (r.type == RING_VSYNC) {
        uint64_t privileged[vsync_bytes / 8];
        memcpy(privileged, data, vsync_bytes);
        vsync_now(privileged, r.arg, int(r.want_pixels), nullptr);
    }
}

void ring_thread() {
    Util::register_thread_index(0); // (Granite's command pools: the sender's when it does the work)
    Ring &q = *ring;
    for (;;) {
        if (q.stop.load()) {
            // the program is ending: nothing more is done, and nothing of the GS's is touched again
            q.stopped.store(true);
            {
                std::lock_guard<std::mutex> hold(q.lock);
                q.done.notify_all();
            }
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        uint64_t tail = q.tail.load(std::memory_order_relaxed);
        if (q.head.load(std::memory_order_acquire) == tail) {
            // nothing to do: a while spinning (packets come in bursts), then asleep
            auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(200);
            while (q.head.load(std::memory_order_acquire) == tail && std::chrono::steady_clock::now() < until) {
                std::this_thread::yield();
            }
            if (q.head.load(std::memory_order_acquire) == tail) {
                std::unique_lock<std::mutex> hold(q.lock);
                q.gs_sleeping.store(true);
                while (q.head.load() == tail && !q.stop.load()) {
                    q.wake.wait(hold);
                }
                q.gs_sleeping.store(false);
            }
            continue;
        }
        const uint8_t *at = q.bytes.data() + (tail & (ring_bytes - 1));
        RingRecord r;
        memcpy(&r, at, sizeof(r));
        ring_work(r, at + sizeof(r));
        if (r.type == RING_VSYNC) {
            q.vsyncs_done.fetch_add(1);
        }
        q.tail.store(tail + sizeof(r) + ((size_t(r.size) + 15) & ~size_t(15)));
        if (q.sender_waiting.load()) {
            std::lock_guard<std::mutex> hold(q.lock);
            q.done.notify_all();
        }
    }
}

// The sender: waits until the GS's thread has done all but `room` bytes of the ring's
template <class Done> void sender_wait(Done done) {
    Ring &q = *ring;
    if (done()) {
        return;
    }
    // a short wait spinning, a long one asleep
    auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(100);
    while (!done() && std::chrono::steady_clock::now() < until) {
        std::this_thread::yield();
    }
    if (!done()) {
        std::unique_lock<std::mutex> hold(q.lock);
        q.sender_waiting.store(true);
        while (!done()) {
            q.done.wait(hold);
        }
        q.sender_waiting.store(false);
    }
}

void ring_wait(size_t room) {
    Ring &q = *ring;
    uint64_t head = q.head.load(std::memory_order_relaxed);
    sender_wait([&] { return head - q.tail.load() <= ring_bytes - room || q.stopped.load(); });
}

// The sender: all that was sent, done
void ring_idle() {
    if (ring) {
        ring_wait(ring_bytes);
    }
}

void ring_put(uint32_t type, int32_t arg, const void *data, size_t size, uint32_t want_pixels = 0) {
    Ring &q = *ring;
    size_t padded = (size + 15) & ~size_t(15);
    size_t need = sizeof(RingRecord) + padded;
    uint64_t head = q.head.load(std::memory_order_relaxed);
    size_t at = head & (ring_bytes - 1);
    if (at + need > ring_bytes) {
        // no room before the ring's end: the rest of it skipped
        ring_wait(ring_bytes - at);
        RingRecord skip = {RING_SKIP, uint32_t(ring_bytes - at - sizeof(RingRecord)), 0, 0};
        memcpy(q.bytes.data() + at, &skip, sizeof(skip));
        head += ring_bytes - at;
        q.head.store(head);
        at = 0;
    }
    ring_wait(need);
    RingRecord r = {type, uint32_t(size), arg, want_pixels};
    memcpy(q.bytes.data() + at, &r, sizeof(r));
    memcpy(q.bytes.data() + at + sizeof(r), data, size);
    q.head.store(head + need);
    if (q.gs_sleeping.load()) {
        std::lock_guard<std::mutex> hold(q.lock);
        q.wake.notify_one();
    }
}

// At exit: the GS's thread stops before the GS's objects are destroyed, once it has done what was sent
// (two seconds at most: what is left then stays undone)
void ring_stop() {
    Ring &q = *ring;
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (q.tail.load() != q.head.load() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    q.stop.store(true);
    {
        std::lock_guard<std::mutex> hold(q.lock);
        q.wake.notify_one();
        q.done.notify_all();
    }
    until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!q.stopped.load() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ring_start() {
    const char *on = getenv("KF4_GSN_THREAD");
    if (on && atoi(on) == 0) {
        return;
    }
    ring = new Ring();
    ring->bytes.resize(ring_bytes);
    std::thread(ring_thread).detach();
    atexit(ring_stop);
}

// ---- The pipeline cache -------------------------------------------------------------------------
// Vulkan pipelines are made as draws first need them (a second or two of them in the first minutes of
// play, when the driver has not kept them itself: a new build, or its own cache full). The pipeline
// cache is kept in the data folder (gsn_pipeline_cache), and written again a while after it grows.
std::string cache_path;
size_t cache_saved_size;
uint32_t cache_vsyncs;

void save_pipeline_cache() {
    size_t size = gs->device->get_pipeline_cache_size();
    if (size == 0 || size == cache_saved_size) {
        return;
    }
    std::vector<uint8_t> data(size);
    if (!gs->device->get_pipeline_cache_data(data.data(), size)) {
        return;
    }
    // (into a file beside it, then in its place: never half written)
    std::string folder = cache_path.substr(0, cache_path.find_last_of("/\\"));
#ifdef _WIN32
    _mkdir(folder.c_str());
#else
    mkdir(folder.c_str(), 0755);
#endif
    std::string temp = cache_path + ".new";
    FILE *f = fopen(temp.c_str(), "wb");
    if (!f) {
        return;
    }
    bool written = fwrite(data.data(), 1, size, f) == size;
    written = fclose(f) == 0 && written;
    if (written) {
        remove(cache_path.c_str());
        written = rename(temp.c_str(), cache_path.c_str()) == 0;
    }
    if (written) {
        cache_saved_size = size;
        GSN_LOG(1, "pipeline cache written: %zu bytes\n", size);
    }
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
        present_wanted = false;
    } else {
        if (!Context::init_loader(nullptr)) {
            fprintf(stderr, "[gsn] no Vulkan loader\n");
            return 0;
        }
        // a presenter's surface and swapchain extensions when asked for; without them if they fail
        std::vector<const char *> instance_ext, device_ext;
        if (present_wanted) {
            present_extensions(instance_ext, device_ext);
        }
        gs->context = new Context;
        gs->context->set_num_thread_indices(1);
        if (!gs->context->init_instance_and_device(instance_ext.data(), uint32_t(instance_ext.size()), device_ext.data(),
                                                   uint32_t(device_ext.size()), 0)) {
            delete gs->context;
            gs->context = nullptr;
            if (!instance_ext.empty()) {
                fprintf(stderr, "[gsn] no Vulkan device with a swapchain: the window is shown without one\n");
                gs->context = new Context;
                gs->context->set_num_thread_indices(1);
                if (!gs->context->init_instance_and_device(nullptr, 0, nullptr, 0, 0)) {
                    delete gs->context;
                    gs->context = nullptr;
                }
            }
            present_wanted = false;
            if (!gs->context) {
                fprintf(stderr, "[gsn] no Vulkan device\n");
                return 0;
            }
        } else if (instance_ext.empty()) {
            present_wanted = false;
        }
        gs->own_context = true;
    }
    const char *level = getenv("KF4_GSN_LOG");
    log_level = level ? atoi(level) : 0;
    gs->device = new Device;
    gs->device->set_context(*gs->context);
    // a frame context a vblank: with four, the CPU waits at a vblank for the GPU's work of four vblanks
    // before (two left it waiting on the GPU with --fast, a third of the native GS's vsync time)
    gs->device->init_frame_contexts(4);
    // --native-cutout-aa shades at every sample (the SPIR-V's SampleRateShading)
    sample_shading = gs->device->get_device_features().enabled_features.sampleRateShading != VK_FALSE;
    make_program();

    gs->dummy_raw = make_image(1, 1, VK_FORMAT_R8_UINT, 1, false);
    gs->dummy_rgba = make_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1, false);
    cmd().clear_image(*gs->dummy_raw, {});
    cmd().clear_image(*gs->dummy_rgba, {});
    transfer_barrier();
    submit(true);
    fprintf(stderr, "[gsn] the native GS on %s\n", gs->device->get_gpu_properties().deviceName);
    // --native-msaa's sample counts: colour and depth attachments' both, and depth read in a shader
    const VkPhysicalDeviceLimits &limits = gs->device->get_gpu_properties().limits;
    msaa_samples_have = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts &
                        limits.sampledImageDepthSampleCounts;
    switch (gs->device->get_gpu_properties().deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        gpu_kind = GSN_GPU_DISCRETE;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        gpu_kind = GSN_GPU_INTEGRATED;
        break;
    default:
        gpu_kind = GSN_GPU_OTHER;
        break;
    }
    ring_start();
    return 1;
}

extern "C" void gsn_gif(int path, const void *data, size_t bytes) {
    if (ring) {
        // (a packet bigger than a quarter of the ring goes in pieces: the GIF reads it qword by qword)
        const uint8_t *p = static_cast<const uint8_t *>(data);
        while (bytes > ring_bytes / 4) {
            ring_put(RING_GIF, path, p, ring_bytes / 4);
            p += ring_bytes / 4;
            bytes -= ring_bytes / 4;
        }
        ring_put(RING_GIF, path, p, bytes);
        return;
    }
    Timer timer(gsn_stats.ns_gif);
    gif(path, static_cast<const uint8_t *>(data), bytes);
}

extern "C" void gsn_wait(void) {
    ring_idle();
}

extern "C" void gsn_pipeline_cache(const char *path) {
    ring_idle();
    std::vector<uint8_t> data;
    if (FILE *f = fopen(path, "rb")) {
        uint8_t buffer[65536];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
            data.insert(data.end(), buffer, buffer + n);
        }
        fclose(f);
    }
    // (made again empty if the file is not this driver's, or damaged)
    gs->device->init_pipeline_cache(data.empty() ? nullptr : data.data(), data.size());
    cache_path = path;
    cache_saved_size = gs->device->get_pipeline_cache_size();
}

extern "C" void gsn_read_fifo(void *dst, uint32_t qwords) {
    ring_idle();
    size_t bytes = size_t(qwords) * 16;
    size_t have = gs->readback.size() > gs->readback_at ? gs->readback.size() - gs->readback_at : 0;
    size_t n = std::min(bytes, have);
    memcpy(dst, gs->readback.data() + gs->readback_at, n);
    memset(static_cast<uint8_t *>(dst) + n, 0, bytes - n);
    gs->readback_at += n;
}

namespace {
// gsn_set_presenter's (a picture's pixels, or a new image for gsn_scanout_take), and the pixels it
// is given
void (*presenter)(const PortGsFrame *frame);
void (*presenter_image)(void);
PortGsFrame presented;

int vsync_now(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame) {
    Timer timer(gsn_stats.ns_vsync);
    if ((want_pixels & GSN_PRESENT) && !(want_pixels & (GSN_WANT_IMAGE | GSN_WANT_PIXELS))) {
        if (!presented.rgba) {
            presented.rgba = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS));
        }
        frame = &presented;
    }
    flush_batch();
    // a register every 16 bytes: PMODE, SMODE1, SMODE2, ..., DISPFB1, DISPLAY1, DISPFB2, DISPLAY2
    auto preg = [&](int n) { return privileged[n * 2]; };
    uint64_t pmode = preg(0);
    uint64_t smode2 = preg(2);
    int circuit = (pmode & 2) ? 2 : (pmode & 1) ? 1 : 0;
    // the PC's switches (gsn_vsync)
    bool progressive = want_pixels & VSYNC_PROGRESSIVE, deinterlace = want_pixels & VSYNC_DEINTERLACE;
    want_pixels &= GSN_WANT_PIXELS | GSN_WANT_IMAGE | GSN_PRESENT;
    // a frame-mode picture read a field at a time: 4 vblanks after a tall SCANMSK primitive (scanmsk_seen)
    bool field_scan = gs->field_scan_vblanks > 0;
    if (gs->field_scan_vblanks) {
        gs->field_scan_vblanks--;
    }
    GSN_LOG(3, "vsync field %d%s PMODE %llx SMODE2 %llx DISPFB1 %llx DISPLAY1 %llx DISPFB2 %llx DISPLAY2 %llx\n", field,
            field_scan ? " (a field at a time)" : "", (unsigned long long)pmode, (unsigned long long)smode2,
            (unsigned long long)preg(7), (unsigned long long)preg(8), (unsigned long long)preg(9),
            (unsigned long long)preg(10));
    bool history_kept = false;
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
        // The scanout ("The scanout"): a field picture woven with the one before (or alone); a frame
        // picture whole, or a field at a time (this field's lines, deinterlaced or alone)
        ScanMode mode = SCAN_WHOLE;
        int parity = 0;
        if (interlaced && field_mode) {
            mode = deinterlace ? SCAN_WEAVE : SCAN_WHOLE;
            parity = field & 1;
        } else if (interlaced && (!progressive || field_scan)) {
            mode = deinterlace ? SCAN_DEINTERLACE : SCAN_FIELD;
            parity = int((bits(display, 12, 11) ^ uint32_t(field)) & 1); // (DY's parity moves the fields)
        }
        uint32_t sx = target_sx, sy = target_sy;
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024 &&
            size_t(w) * h * sx * sy * (mode == SCAN_WEAVE ? 2 : 1) <= PORT_GS_FRAME_PIXELS) {
            Shown shown = shown_buffer(fbp, fbw, psm, dbx, dby, w, h);
            if (mode == SCAN_DEINTERLACE) {
                record_history(shown, parity);
                history_kept = true;
            }
            if (want_pixels & GSN_WANT_IMAGE) {
                ok = scanout_to_image(shown, mode, parity, (want_pixels & GSN_WANT_PIXELS) ? frame : nullptr);
            } else if (mode == SCAN_WEAVE) {
                // each field its own picture, woven with the one before: kept every field, wanted or not
                int f = field & 1;
                uint32_t got[2];
                gs->fields[f].resize(size_t(w) * h * sx * sy);
                read_buffer(fbp, fbw, psm, dbx, dby, w, h, gs->fields[f].data(), true, got);
                if (got[0] != sx || got[1] != sy) { // not a target (yet): one pixel a GS pixel, repeated
                    std::vector<uint32_t> small(gs->fields[f].begin(), gs->fields[f].begin() + long(size_t(w) * h));
                    for (uint32_t y = 0; y < h * sy; y++) {
                        for (uint32_t x = 0; x < w * sx; x++) {
                            gs->fields[f][size_t(y) * w * sx + x] = small[size_t(y / sy) * w + x / sx];
                        }
                    }
                }
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
                    // the woven picture's line l is field (l & 1)'s line l / 2, each line sy rows of pixels
                    for (uint32_t y = 0; y < h * 2 * sy; y++) {
                        uint32_t l = y / sy, row = (l / 2) * sy + y % sy;
                        memcpy(frame->rgba + size_t(y) * w * sx, gs->fields[l & 1].data() + size_t(row) * w * sx, w * sx * 4);
                    }
                    frame->width = w * sx;
                    frame->height = h * 2 * sy;
                    ok = 1;
                }
            } else if (mode != SCAN_WHOLE || (shown.t && wide(*shown.t->image))) {
                // (on the GPU, as for the presenter, and read back; --native-colour high's picture so
                // too, dithered as the window has it)
                if (want_pixels) {
                    ok = (scanout_to_image(shown, mode, parity, frame) & GSN_WANT_PIXELS) ? 1 : 0;
                }
            } else if (want_pixels) {
                uint32_t got[2];
                read_buffer(fbp, fbw, psm, dbx, dby, w, h, frame->rgba, true, got);
                frame->width = w * got[0];
                frame->height = h * got[1];
                ok = 1;
            }
        }
    }
    if (!history_kept && gs->history_filled) {
        forget_history();
    }
    submit(false);
    gs->device->next_frame_context();
    if (ok & GSN_WANT_IMAGE) {
        scanout_published();
    }
    // the pipeline cache, written when it has grown (looked at every ten seconds)
    if (!cache_path.empty() && ++cache_vsyncs % 600 == 0) {
        save_pipeline_cache();
    }
    gsn_stats.targets = gs->targets.size();
    gsn_stats.textures = gs->textures.size();
    gsn_stats.pool_images = decoded_pool.size();
    gsn_stats.pool_mb = decoded_pool_bytes >> 20;
    gsn_stats.upload_copies = page_images.size();
    uint32_t wanted = wanted_scale;
    if ((wanted & 0xFF) != target_sx || (wanted >> 8) != target_sy || wanted_samples != target_samples ||
        wanted_colour != target_colour) {
        // every target back to VRAM; they come again at the new size as they are drawn into
        for (auto &t : gs->targets) {
            write_back(*t);
        }
        end_pass();
        gs->targets.clear();
        target_sx = wanted & 0xFF;
        target_sy = wanted >> 8;
        target_samples = wanted_samples;
        target_colour = wanted_colour;
        gs->fields[0].clear();
        gs->fields[1].clear();
        forget_history();
    }
    if (ok && (want_pixels & GSN_PRESENT)) {
        // the window's picture, handed over now that it is there (an image: once its commands are on
        // the queue, scanout_published above)
        if ((ok & GSN_WANT_IMAGE) && presenter_image) {
            presenter_image();
        } else if (!(want_pixels & GSN_WANT_IMAGE) && presenter) {
            presenter(frame);
        }
    }
    return ok;
}
} // namespace

extern "C" void gsn_set_presenter(void (*pixels)(const PortGsFrame *frame), void (*image)(void)) {
    presenter = pixels;
    presenter_image = image;
}

extern "C" int gsn_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame) {
    // the PC's switches for the scanout, as they are at this vblank
    want_pixels |= (port_options.progressive ? VSYNC_PROGRESSIVE : 0) | (port_options.deinterlace ? VSYNC_DEINTERLACE : 0);
    // On the GS's thread, with the sender going on: a vblank whose picture nobody waits for (none, or
    // one the GS's thread hands to the presenter). A picture wanted in frame, or an image the caller
    // goes on to show itself, waits for the thread to be idle and is made here.
    bool queued = !(want_pixels & GSN_WANT_PIXELS) && (!(want_pixels & GSN_WANT_IMAGE) || (want_pixels & GSN_PRESENT));
    if (ring && queued) {
        ring_put(RING_VSYNC, field, privileged, vsync_bytes, uint32_t(want_pixels));
        ring->vsyncs_sent++;
        if (want_pixels & GSN_PRESENT) {
            // A window's picture is at most two vblanks behind the game: the sender waits for the GS's
            // thread beyond that (with --fast it would run ahead by as many vblanks as the ring holds)
            Ring &q = *ring;
            sender_wait([&] { return q.vsyncs_sent - q.vsyncs_done.load() <= 2 || q.stopped.load(); });
        }
        return want_pixels & (GSN_PRESENT | GSN_WANT_IMAGE);
    }
    ring_idle();
    return vsync_now(privileged, field, want_pixels, frame);
}

extern "C" void gsn_set_scale(int x_scale, int y_scale) {
    uint32_t x = uint32_t(x_scale < 1 ? 1 : x_scale > GSN_MAX_SCALE_X ? GSN_MAX_SCALE_X : x_scale);
    uint32_t y = uint32_t(y_scale < 1 ? 1 : y_scale > 4 ? 4 : y_scale);
    wanted_scale = x | y << 8;
}

extern "C" int gsn_gpu_kind(void) {
    return gpu_kind;
}

extern "C" void gsn_set_lod(int mode) {
    lod_mode = mode;
}

extern "C" int gsn_set_aniso(int taps) {
    int l = 0;
    while (l < 4 && (2 << l) <= taps) {
        l++;
    }
    aniso_log2 = l;
    return 1 << l;
}

extern "C" void gsn_set_cutout_aa(int mode) {
    cutout_aa_mode = mode;
}

extern "C" void gsn_set_colour(int mode) {
    wanted_colour = mode == GSN_COLOUR_HIGH ? GSN_COLOUR_HIGH : GSN_COLOUR_EXACT;
}

extern "C" int gsn_set_msaa(int samples) {
    // the most this GPU has up to `samples` (msaa_samples_have)
    uint32_t n = 1;
    while (n * 2 <= uint32_t(samples) && n < 8 && (msaa_samples_have & (n * 2))) {
        n *= 2;
    }
    // (off and on again is live; another count once one has been drawn with, from the next start)
    uint32_t used = msaa_samples_used.load();
    if (n > 1 && used) {
        n = used;
    }
    wanted_samples = n;
    return int(n);
}

extern "C" int gsn_display_rect(const uint64_t *privileged, GsnRect *rect) {
    return display_rect(privileged, rect);
}

extern "C" void gsn_read_rect(const GsnRect *r, uint32_t *rgba) {
    ring_idle();
    flush_batch();
    read_buffer(r->bp, r->bw, r->psm, r->x, r->y, r->w, r->h, rgba);
    submit(false);
}

extern "C" void gsn_read_rect_scaled(const GsnRect *r, uint32_t *rgba, uint32_t *scale) {
    ring_idle();
    flush_batch();
    read_buffer(r->bp, r->bw, r->psm, r->x, r->y, r->w, r->h, rgba, true, scale);
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
    ring_idle();
    flush_batch();
    Target *t = find_target(r->bp, r->bw, r->psm);
    if (t && t->sx == 1 && t->sy == 1 && r->x + r->w <= t->width && r->y + r->h <= t->height) {
        msaa_settle(*t);
        msaa_image_written(*t);
        end_pass();
        transfer_barrier();
        void *out = cmd().update_image(*t->image, {int32_t(r->x), int32_t(r->y), 0}, {r->w, r->h, 1}, r->w, r->h,
                                       {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
        put_pixels(out, *t->image, 0, rgba, size_t(r->w) * r->h);
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
    ring_idle();
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

    // the scanout's history, as paraLLEl-GS's: the countdown, then the four fields (at the GS's
    // resolution only: a larger --native-scale's are left out, as paraLLEl-GS could not use them)
    std::vector<uint8_t> history;
    auto put = [&](const void *data, size_t size) {
        history.insert(history.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    };
    uint32_t head[2] = {gs->field_scan_vblanks, 4};
    put(head, sizeof(head));
    bool gs_size = target_sx == 1 && target_sy == 1;
    for (uint32_t i = 0; i < 4; i++) {
        Kf4GsField f = {-1, 0, 0, 0};
        const Image *image = gs_size && i < gs->history_filled ? gs->history[i].get() : nullptr;
        if (image) {
            f.present = 1;
            f.width = image->get_width();
            f.height = image->get_height();
            for (uint32_t j = 0; j < i; j++) {
                if (gs->history[j].get() == image) {
                    f.same_as = int32_t(j);
                }
            }
        }
        put(&f, sizeof(f));
        if (image && f.same_as < 0) {
            flush_batch();
            const uint8_t *pixels = read_image(*image, 0, 0, f.width, f.height);
            put(pixels, size_t(f.width) * f.height * 4);
            done_reading();
        }
    }
    emit(KF4GS_GS_HISTORY, history.data(), history.size());
}

extern "C" size_t gsn_state_size(void) {
    return GSN_VRAM_BYTES + sizeof(gs->reg) + sizeof(gs->internal_q) + sizeof(gs->clut) + sizeof(gs->clut_cbp) +
           sizeof(gs->queue) + sizeof(gs->queue_count) + sizeof(gs->paths) + sizeof(gs->upload) + sizeof(gs->transfer_dir);
}

extern "C" void gsn_state(PortStateIo *io) {
    ring_idle();
    if (!io->loading) {
        port_state_bytes(io, const_cast<void *>(gsn_vram()), GSN_VRAM_BYTES);
    } else {
        flush_batch();
        end_pass();
        gs->targets.clear();
        gs->textures.clear();
        texture_index.clear();
        forget_uploads();
        port_state_bytes(io, gs->vram.data(), GSN_VRAM_BYTES);
        pages_written(0, vram_pages);
    }
    port_state_bytes(io, gs->reg, sizeof(gs->reg));
    port_state_bytes(io, &gs->internal_q, sizeof(gs->internal_q));
    port_state_bytes(io, gs->clut, sizeof(gs->clut));
    port_state_bytes(io, gs->clut_cbp, sizeof(gs->clut_cbp));
    port_state_bytes(io, gs->queue, sizeof(gs->queue));
    port_state_bytes(io, &gs->queue_count, sizeof(gs->queue_count));
    port_state_bytes(io, gs->paths, sizeof(gs->paths));
    port_state_bytes(io, &gs->upload, sizeof(gs->upload));
    port_state_bytes(io, &gs->transfer_dir, sizeof(gs->transfer_dir));
    if (io->loading) {
        for (uint32_t i = 0; i < 3; i++) {
            gs->queued[i] = make_vtx(gs->queue[i], effective_prim(), gs->queue[i]);
        }
        gs->verts.clear();
        gs->fields[0].clear();
        gs->fields[1].clear();
        forget_history();
    }
}

extern "C" int gsn_restore(uint32_t type, const void *data, size_t size) {
    ring_idle();
    switch (type) {
    case KF4GS_VRAM:
        if (size != GSN_VRAM_BYTES) {
            return 0;
        }
        flush_batch();
        end_pass();
        gs->targets.clear();
        gs->textures.clear();
        texture_index.clear();
        forget_uploads();
        memcpy(gs->vram.data(), data, GSN_VRAM_BYTES);
        pages_written(0, vram_pages);
        reset_gs();
        forget_history(); // (a capture's comes after: KF4GS_GS_HISTORY)
        // (no targets: a capture is drawn with MSAA, and at the scale, from its start)
        target_samples = wanted_samples;
        target_colour = wanted_colour;
        target_sx = wanted_scale & 0xFF;
        target_sy = wanted_scale >> 8;
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
        return 1;
    case KF4GS_GS_HISTORY: {
        // the countdown and the fields, as gsn_capture writes them (or paraLLEl-GS)
        const uint8_t *at = static_cast<const uint8_t *>(data), *end = at + size;
        uint32_t head[2];
        if (size < sizeof(head)) {
            return 0;
        }
        memcpy(head, at, sizeof(head));
        at += sizeof(head);
        gs->field_scan_vblanks = head[0];
        forget_history();
        ImageHandle fields[4];
        for (uint32_t i = 0; i < head[1] && i < 4; i++) {
            Kf4GsField f;
            if (size_t(end - at) < sizeof(f)) {
                return 0;
            }
            memcpy(&f, at, sizeof(f));
            at += sizeof(f);
            if (!f.present) {
                continue;
            }
            if (f.same_as >= 0 && uint32_t(f.same_as) < i) {
                fields[i] = fields[f.same_as];
                continue;
            }
            size_t bytes = size_t(f.width) * f.height * 4;
            if (size_t(end - at) < bytes || !f.width || !f.height || f.width > 8192 || f.height > 8192) {
                return 0;
            }
            fields[i] = make_image(f.width, f.height, VK_FORMAT_R8G8B8A8_UNORM, 1, true);
            end_pass();
            transfer_barrier();
            void *out = cmd().update_image(*fields[i], {0, 0, 0}, {f.width, f.height, 1}, f.width, f.height,
                                           {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
            memcpy(out, at, bytes);
            transfer_barrier();
            at += bytes;
        }
        // the fields there from the newest on (paraLLEl-GS's fills a missing one as its deinterlace does)
        while (gs->history_filled < 4 && fields[gs->history_filled]) {
            gs->history[gs->history_filled] = fields[gs->history_filled];
            gs->history_filled++;
        }
        return 1;
    }
    }
    return 0;
}

// ---- The presenter's side (port/platform/present_vk.cpp) ------------------------------------------
extern "C" void gsn_enable_present(void) {
    present_wanted = true;
}

extern "C" int gsn_present_device(GsnPresentDevice *out) {
    if (!gs || !gs->context || !present_wanted) {
        return 0;
    }
    const QueueInfo &queues = gs->context->get_queue_info();
    out->instance = gs->context->get_instance();
    out->gpu = gs->context->get_gpu();
    out->device = gs->context->get_device();
    out->queue = queues.queues[QUEUE_INDEX_GRAPHICS];
    out->queue_family = queues.family_indices[QUEUE_INDEX_GRAPHICS];
    out->api_version = gs->context->get_enabled_device_features().instance_api_core_version;
    out->get_instance_proc_addr = reinterpret_cast<void *>(Context::get_instance_proc_addr());
    return out->queue != VK_NULL_HANDLE;
}

extern "C" void gsn_queue_lock(void) {
    gs->device->external_queue_lock();
}

extern "C" void gsn_queue_unlock(void) {
    gs->device->external_queue_unlock();
}

extern "C" int gsn_scanout_take(GsnScanout *out) {
    std::lock_guard<std::mutex> hold(scanout_image.lock);
    if (!scanout_image.image || scanout_image.serial == 0) {
        return 0;
    }
    out->image = uint64_t(scanout_image.image->get_image());
    out->width = scanout_image.width;
    out->height = scanout_image.height;
    out->serial = scanout_image.serial;
    scanout_image.taken = true;
    return 1;
}

extern "C" void gsn_scanout_release(void) {
    std::lock_guard<std::mutex> hold(scanout_image.lock);
    scanout_image.taken = false;
}
