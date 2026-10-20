/* The Vulkan presenter (port/platform/present_vk.cpp, --presenter vulkan): the window's swapchain on
   the native GS's own Vulkan device. It draws the native GS's picture straight from the GPU (no
   read-back), or the pixels another renderer gave, scaled into the window as port_video_fit says, then
   the settings overlay (Dear ImGui's Vulkan backend) over it. All of it on the game's thread. */
#ifndef PORT_PRESENT_VK_H
#define PORT_PRESENT_VK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* (port/platform/video.c) the picture's place in a window of out_w x out_h pixels */
void port_video_fit(int out_w, int out_h, int *x, int *y, int *width, int *height);

#ifdef KF4_HAVE_VULKAN_PRESENTER
/* The swapchain on the window (an SDL_Window made with SDL_WINDOW_VULKAN): 1 when it is up. 0 when
   it cannot be (no native GS, or its device has no swapchain, or the surface fails), and the window
   is then SDL_Renderer's. */
int port_vk_open(void *sdl_window);
/* The vblank's picture: pixels (width x height, R in the low byte) to upload, or with image the
   native GS's (gsn_scanout_take); with neither, the last one again, and only if the overlay draws.
   Presented with the overlay over it. */
void port_vk_present(const uint32_t *pixels, uint32_t width, uint32_t height, int image);
/* Waits for the screen's refresh (FIFO) or not (immediate, else mailbox), from the next picture */
void port_vk_set_vsync(int on);
/* (port/overlay/overlay.cpp) Dear ImGui's Vulkan backend on the presenter's render pass: 1 when up */
int port_vk_imgui_init(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
