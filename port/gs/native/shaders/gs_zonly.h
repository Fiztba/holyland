// The Z of gs_frag.h (GSN_ZCLAMP) alone: gs_zonly.frag and gs_zonly_sample.frag. The same input and the
// same arithmetic, so the depth is gs_zclamp.frag's to the bit.
#include "gs_common.h"

#ifdef GSN_SAMPLE
layout(location = 3) sample noperspective in float v_z;
#else
layout(location = 3) noperspective in float v_z;
#endif

uint field(uint v, uint shift, uint bits) {
    return (v >> shift) & ((1u << bits) - 1u);
}

void main() {
    float z = min(v_z, float(pc.zmax));
    if ((pc.misc & GSN_M_ZFINE) == 0u) {
        z = roundEven(z);
    }
    gl_FragDepth = min(z + pc.zbias, float(pc.zmax)) * pc.zscale;
    uint scanmsk = field(pc.misc, GSN_M_SCANMSK_SHIFT, 2u);
    uint line = uint(gl_FragCoord.y) / max(field(pc.misc, GSN_M_SCALE_SHIFT, 3u), 1u);
    if (scanmsk >= 2u && (line & 1u) == scanmsk - 2u) {
        discard;
    }
}
