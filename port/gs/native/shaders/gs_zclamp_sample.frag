#version 450
#extension GL_GOOGLE_include_directive : require
// gs_zclamp.frag shaded once a sample of a multisampled target instead of once a pixel: the alpha
// test's cut-outs with --native-cutout-aa ssaa (port/gs/native/gsn.cpp)
#define GSN_ZCLAMP 1
#define GSN_SAMPLE 1
#include "gs_frag.h"
