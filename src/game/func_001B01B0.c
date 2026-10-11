typedef struct {
    float x;
    float y;
    float z;
    int w;
} Vec4;

float func_001B01B0(float *arg0, float *arg1) {
    Vec4 out;
    Vec4 v;

    v.x = arg1[0];
    v.y = arg0[1];
    v.z = arg1[2];
    v.w = 0;
    __asm__ volatile(
        "lqc2 $vf16, 0(%0)\n"
        "lqc2 $vf17, 0(%1)\n"
        "vsub.xyz $vf16, $vf17, $vf16\n"
        "vmul.xyz $vf17, $vf16, $vf16\n"
        "vaddx.z $vf17, $vf17, $vf17x\n"
        "vaddy.z $vf17, $vf17, $vf17y\n"
        ".word 0x4B1103BD\n"
        ".word 0x4A0003BF\n"
        "vaddq.x $vf16, $vf0, Q\n"
        "sqc2 $vf16, 0(%2)\n"
        :
        : "r"(arg0), "r"(&v), "r"(&out)
        : "memory");
    return out.x;
}
