#version 450
#extension GL_GOOGLE_include_directive : require
// gs_zclamp_sample.frag blending from the resolved copy of the destination kept in the render pass
// (KF4_GSN_MSAA_BLEND=fast: port/gs/native/gsn.cpp, "Shader blending and the render pass")
#define GSN_ZCLAMP 1
#define GSN_SAMPLE 1
#define GSN_DEST_INPUT 2
#include "gs_frag.h"
