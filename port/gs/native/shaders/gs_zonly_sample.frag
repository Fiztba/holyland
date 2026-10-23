#version 450
#extension GL_GOOGLE_include_directive : require
// gs_zonly.frag at every sample (the Z pass of a layer gs_zclamp_sample.frag shades)
#define GSN_SAMPLE 1
#include "gs_zonly.h"
