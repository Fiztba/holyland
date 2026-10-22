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
/* GIF packets and vblanks (without a picture wanted) are worked on by the native GS's own thread; the
   functions that give something back wait for it to have done all that was sent before */
void gsn_gif(int path, const void *data, size_t bytes);
/* Waits until the native GS's thread has done all that was sent to it */
void gsn_wait(void);
/* The Vulkan pipeline cache kept in this file: read now (if it is there and this driver's), and
   written again from time to time when pipelines have been made */
void gsn_pipeline_cache(const char *path);
void gsn_read_fifo(void *dst, uint32_t qwords);
/* The vblank: the display registers (PMODE..BGCOLOR, a register every 16 bytes), and the picture
   into frame when want_pixels (1 if there is one). With GSN_WANT_IMAGE in want_pixels the picture
   stays on the GPU instead, as the image gsn_scanout_take hands out (GSN_WANT_IMAGE is returned when
   it is there; GSN_WANT_PIXELS as well asks for it in frame too, read back from that image).
   With GSN_PRESENT the picture goes to the presenter (gsn_set_presenter): its pixels, or with
   GSN_WANT_IMAGE word that the image is new, from the native GS's thread once it is there. Without
   GSN_WANT_PIXELS the caller then does not wait for it (and gets GSN_PRESENT, with GSN_WANT_IMAGE if
   asked); a vblank with no picture wanted does not wait either. */
#define GSN_WANT_PIXELS 1
#define GSN_WANT_IMAGE 2
#define GSN_PRESENT 4
int gsn_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);
void gsn_set_presenter(void (*pixels)(const PortGsFrame *frame), void (*image)(void));
/* The render targets' size, from the next vblank: x_scale times the PS2's across (1 to GSN_MAX_SCALE_X)
   and y_scale times down (1 to 4). --native-scale gives both the same; --native-widescreen a wider x. */
#define GSN_MAX_SCALE_X 6
void gsn_set_scale(int x_scale, int y_scale);
/* --native-lod: the mipmap level a texture is read at. The GS picks it from the distance (Q), for the
   PS2's 640x448: GSN_LOD_PS2 keeps that; GSN_LOD_SCALE (the default) takes it log2 of --native-scale
   finer, the detail the GS would give the larger picture (at 1x, the PS2's); GSN_LOD_FULL reads
   level 0 at every distance (at 1x too). From the next draw. */
#define GSN_LOD_PS2 0
#define GSN_LOD_SCALE 1
#define GSN_LOD_FULL 2
void gsn_set_lod(int mode);
/* --native-msaa: the samples a pixel of the targets the 3D is drawn into has, from the next vblank
   (1, the default, is the PS2's picture; 2, 4 or 8). Returns the count used: the most the GPU has up
   to `samples`. */
int gsn_set_msaa(int samples);
/* --native-colour: how precisely colours are kept, from the next vblank. GSN_COLOUR_EXACT (the default)
   is the GS's: 8 bits a channel, every step of its arithmetic rounded as the GS rounds it, the PS2's
   picture. GSN_COLOUR_HIGH keeps the 32-bit targets at 16 bits a channel and the colour arithmetic in
   floats (the texture filter, texture function, fog, blending, and the frame's passes that read it
   again), alpha and 16-bit targets as the GS has them; the picture is dithered to 8 bits as it is
   scanned out, and anything read back (write-backs to VRAM, captures) is rounded to 8 bits. */
#define GSN_COLOUR_EXACT 0
#define GSN_COLOUR_HIGH 1
void gsn_set_colour(int mode);

/* A rectangle of a GS buffer: base (256-byte blocks), width (64-pixel units), format, place, size */
typedef struct {
    uint32_t bp, bw, psm, x, y, w, h;
} GsnRect;
/* The buffer the display shows at this vblank (circuit 2, else 1; a field's 224 lines in field mode):
   0 if none */
int gsn_display_rect(const uint64_t *privileged, GsnRect *rect);
/* A rectangle as RGBA (alpha 255): from the native GS's targets, or its VRAM */
void gsn_read_rect(const GsnRect *rect, uint32_t *rgba);
/* The same at the target's own pixels (--native-scale): rect->w * scale[0] by rect->h * scale[1] of
   them into rgba (room for GSN_MAX_SCALE_X * 4 times the rectangle), the scale across and down into
   scale (1 and 1 where it is not a target) */
void gsn_read_rect_scaled(const GsnRect *rect, uint32_t *rgba, uint32_t *scale);
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
