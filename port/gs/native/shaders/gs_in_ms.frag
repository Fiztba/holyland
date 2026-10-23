#version 450
#extension GL_GOOGLE_include_directive : require
// gs.frag blending from the resolved copy of the destination kept in the render pass, with
// KF4_GSN_MSAA_BLEND=fast (input attachment 0 of a multisampled target's pass: port/gs/native/gsn.cpp,
// "Shader blending and the render pass")
#define GSN_DEST_INPUT 2
#include "gs_frag.h"
