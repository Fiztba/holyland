/* The native GS (docs/port/STEP3.md): the GS drawn with the GPU's own rasteriser, through Vulkan
   (Granite's layer, the one paraLLEl-GS runs on), instead of emulated in compute by paraLLEl-GS.

   It takes what port/platform/gs.c hands the GS backend (GIF packets, the read-back FIFO, the vblank's
   display registers) and gives the same kind of picture back. port/gs/native/gsn.cpp says how. */
#ifndef PORT_GS_NATIVE_GSN_H
#define PORT_GS_NATIVE_GSN_H

#include <stddef.h>
#include <stdint.h>

#include "port/platform.h"
#include "port/savestate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts it: on the Vulkan context paraLLEl-GS made (a Vulkan::Context *, to run beside it and compare)
   or, with NULL, on one of its own. 0 if there is no Vulkan device it can use. */
int gsn_open(void *shared_context);
void gsn_gif(int path, const void *data, size_t bytes);
void gsn_read_fifo(void *dst, uint32_t qwords);
/* The vblank: the display registers (PMODE..BGCOLOR, a register every 16 bytes), and the picture
   into frame when want_pixels (1 if there is one). With GSN_WANT_IMAGE in want_pixels the picture
   stays on the GPU instead, as the image gsn_scanout_take hands out (GSN_WANT_IMAGE is returned when
   it is there; GSN_WANT_PIXELS as well asks for it in frame too, read back from that image). */
#define GSN_WANT_PIXELS 1
#define GSN_WANT_IMAGE 2
int gsn_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);
/* The render targets' size, from the next vblank: x_scale times the PS2's across (1 to GSN_MAX_SCALE_X)
   and y_scale times down (1 to 4). --native-scale gives both the same; --native-widescreen a wider x. */
#define GSN_MAX_SCALE_X 6
void gsn_set_scale(int x_scale, int y_scale);
/* --native-msaa: the samples a pixel of the targets the 3D is drawn into has, from the next vblank
   (1, the default, is the PS2's picture; 2, 4 or 8). Returns the count used: the most the GPU has up
   to `samples`. */
int gsn_set_msaa(int samples);

/* A rectangle of a GS buffer: base (256-byte blocks), width (64-pixel units), format, place, size */
typedef struct {
    uint32_t bp, bw, psm, x, y, w, h;
} GsnRect;
/* The buffer the display shows at this vblank (circuit 2, else 1; a field's 224 lines in field mode):
   0 if none */
int gsn_display_rect(const uint64_t *privileged, GsnRect *rect);
/* A rectangle as RGBA (alpha 255): from the native GS's targets, or its VRAM */
void gsn_read_rect(const GsnRect *rect, uint32_t *rgba);
/* The same from a VRAM image of the GS's (paraLLEl-GS's, to compare), alpha as the buffer has it */
void gsn_decode_rect(const uint8_t *vram, const GsnRect *rect, uint32_t *rgba);
/* A rectangle of a buffer made these pixels (as gsn_decode_rect gives them): kf4-gsreplay's
   --compare-renderers starts each draw from paraLLEl-GS's picture */
void gsn_write_rect(const GsnRect *rect, const uint32_t *rgba);

/* The GS's state in the GS capture's records (port/include/port/gs_capture_format.h): what a capture,
   a save state or kf4-gsreplay start from. gsn_vram is the 4 MB as the GS has it, render targets
   written back. */
const void *gsn_vram(void);
void gsn_capture(void (*emit)(uint32_t type, const void *data, size_t size));
int gsn_restore(uint32_t type, const void *data, size_t size);

/* Save states (port/platform/savestate.c, through port/platform/gs.c): the native GS's state, VRAM
   first (4 MB, as the GS lays it out, render targets written back), then its registers and the CLUT
   buffer. Taken between GIF packets. */
size_t gsn_state_size(void);
void gsn_state(PortStateIo *io);

/* A presenter on the native GS's own Vulkan device (port/platform/present_vk.cpp): the window's
   swapchain takes the picture straight from the GPU, with no read-back.
   gsn_enable_present, before gsn_open: the instance gets the surface extensions and the device the
   swapchain's, when the Vulkan loader and the GPU have them. gsn_present_device then hands out the
   device (0 if it has no swapchain); the presenter submits to the native GS's queue, between
   gsn_queue_lock and gsn_queue_unlock. Handles are Vulkan's, as void pointers and 64-bit integers. */
typedef struct {
    void *instance;           /* VkInstance */
    void *gpu;                /* VkPhysicalDevice */
    void *device;             /* VkDevice */
    void *queue;              /* VkQueue: the graphics queue the native GS draws on */
    uint32_t queue_family;
    uint32_t api_version;     /* the instance's */
    void *get_instance_proc_addr; /* PFN_vkGetInstanceProcAddr */
} GsnPresentDevice;
void gsn_enable_present(void);
int gsn_present_device(GsnPresentDevice *out);
void gsn_queue_lock(void);
void gsn_queue_unlock(void);
/* The picture of the last vblank asked for with GSN_WANT_IMAGE: an RGBA8 image (VkImage) in the
   GENERAL layout, width x height. The native GS writes it again at each vblank, with a barrier after
   whatever was submitted to the queue before, so a presenter's command buffer, submitted to the same
   queue with a barrier before it reads, sees one whole picture. Between take and release (once the
   presenter's command buffer is submitted) the image is not destroyed. serial counts the pictures. */
typedef struct {
    uint64_t image;
    uint32_t width, height;
    uint64_t serial;
} GsnScanout;
int gsn_scanout_take(GsnScanout *out);
void gsn_scanout_release(void);

/* What the last frames cost and did (--stats, the overlay) */
typedef struct {
    uint64_t draws, primitives, passes, texture_decodes, texture_bytes, target_copies, writebacks,
        uploads, upload_bytes, shader_blends, texture_reuses, uploads_skipped, uploads_copied;
    /* nanoseconds spent (nested: a flush inside the GIF's time counts in both) */
    uint64_t ns_gif, ns_flush, ns_upload, ns_writeback, ns_decode, ns_vsync, ns_swizzle, ns_sync;
    /* what it holds now: render targets, textures, decoded images (and their MB), uploads kept */
    uint64_t targets, textures, pool_images, pool_mb, upload_copies;
    uint64_t ns_draw_setup, ns_draw_record; /* a draw's state worked out; its Vulkan commands */
    uint64_t ms_resolves, ms_loads; /* --native-msaa: multisampled images resolved, and loaded from their image */
} GsnStats;
extern GsnStats gsn_stats;

#ifdef __cplusplus
}
#endif

#endif
