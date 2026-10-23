#version 450
#extension GL_GOOGLE_include_directive : require
// --native-layer-z tolerant: a layer's Z pass with no alpha test to pass (port/gs/native/gsn.cpp,
// flush_batch): only the Z gs_zclamp.frag writes, worked out as it does, and SCANMSK's lines left out.
// No texture, colour or blending: the pass writes no colour.
#include "gs_zonly.h"
