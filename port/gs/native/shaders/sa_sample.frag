#version 450
#extension GL_GOOGLE_include_directive : require
// The scene's cut-outs shaded at every sample (P8, gsn_standalone.inc; --native-cutout-aa ssaa): the native
// GS's pixels (gs_frag.h) at each sample, without its Z (the scene's depth is the vertices')
#define GSN_SAMPLE
#include "gs_frag.h"
