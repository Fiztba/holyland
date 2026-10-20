/* The native GS (docs/port/STEP3.md): the GS drawn with the GPU's own rasteriser, through Vulkan
   (Granite's layer, the one paraLLEl-GS runs on), instead of emulated in compute by paraLLEl-GS.

   It takes what port/platform/gs.c hands the GS backend (GIF packets, the read-back FIFO, the vblank's
   display registers) and gives the same kind of picture back. port/gs/native/gsn.cpp says how. */
#ifndef PORT_GS_NATIVE_GSN_H
#define PORT_GS_NATIVE_GSN_H

#include <stddef.h>
#include <stdint.h>

#include "port/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts it: on the Vulkan context paraLLEl-GS made (a Vulkan::Context *, to run beside it and compare)
   or, with NULL, on one of its own. 0 if there is no Vulkan device it can use. */
int gsn_open(void *shared_context);
void gsn_gif(int path, const void *data, size_t bytes);
void gsn_read_fifo(void *dst, uint32_t qwords);
/* The vblank: the display registers (PMODE..BGCOLOR, a register every 16 bytes), and the picture
   into frame when want_pixels (1 if there is one) */
int gsn_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);

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

/* What the last frames cost and did (--stats, the overlay) */
typedef struct {
    uint64_t draws, primitives, passes, texture_decodes, texture_bytes, target_copies, writebacks,
        uploads, upload_bytes, shader_blends;
} GsnStats;
extern GsnStats gsn_stats;

#ifdef __cplusplus
}
#endif

#endif
