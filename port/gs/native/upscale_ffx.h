// The temporal upscalers' own calls (--upscaler, docs/port/UPSCALING.md): AMD's FSR 3.1 through the FidelityFX API,
// its signed DLL (amd_fidelityfx_vk.dll, beside kf4.exe or KF4_FFX_DLL) loaded at run time. Apart from gsn.cpp, so
// that windows.h and the FFX headers stay out of it; the back end's thread makes these calls (gsn_upscale.inc).
#pragma once
#include <stdint.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h> // (not vulkan.h: its Windows header would come into gsn.cpp)

// An image the upscaler reads or writes: in VK_IMAGE_LAYOUT_GENERAL, as all the native GS's are, and left so
struct UpscaleImage {
    VkImage image;
    VkFormat format;
    uint32_t width, height;
};

// A picture to upscale. color is the scene's picture (perceptual colours: the PS2's), depth its depth as 1 / the
// view's depth times near_plane (R32F: 1 at near_plane, 0 far away, inverted and infinite), motion where each pixel
// was in the last picture less where it is now (RG16F, the scene's pixels, without the jitter), output at the
// upscaled size (with VK_IMAGE_USAGE_STORAGE_BIT). render_* is the part of color drawn (its whole, here).
struct UpscaleFrame {
    VkCommandBuffer cmd;
    UpscaleImage color, depth, motion, output;
    uint32_t render_w, render_h;
    float jitter[2]; // the picture's jitter, in the scene's pixels (down: +y)
    float frame_ms;  // since the last picture
    float near_plane, fov_y;
    bool reset; // nothing to take from the pictures before (the first, after a cut or a while without)
};

// The DLL and its functions, loaded once: false (said why, once) where it is not there
bool ffx_upscale_load();
// A context for pictures of render_* upscaled to out_*: null (said why) where it could not be made
void *ffx_upscale_make(VkDevice device, VkPhysicalDevice gpu, PFN_vkGetDeviceProcAddr proc, uint32_t render_w,
                       uint32_t render_h, uint32_t out_w, uint32_t out_h);
// Its calls for one picture, into f.cmd (compute: its pipelines and descriptor sets bound past Granite)
bool ffx_upscale_dispatch(void *context, const UpscaleFrame &f);
// (once the GPU is done with the pictures it made)
void ffx_upscale_free(void *context);
