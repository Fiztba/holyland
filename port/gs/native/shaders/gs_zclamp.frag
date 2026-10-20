#version 450
#extension GL_GOOGLE_include_directive : require
// The native GS's pixels, with Z kept within the Z buffer's format per pixel: gs_frag.h
#define GSN_ZCLAMP 1
#include "gs_frag.h"
