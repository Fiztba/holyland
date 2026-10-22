#version 450
#extension GL_GOOGLE_include_directive : require
// gs_zclamp.frag blending from the copy of the destination kept in the render pass (input attachment 1
// of a single-sampled target's pass: port/gs/native/gsn.cpp, "Shader blending and the render pass")
#define GSN_ZCLAMP 1
#define GSN_DEST_INPUT 1
#include "gs_frag.h"
