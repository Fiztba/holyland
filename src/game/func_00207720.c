void func_00207720(void *dst, void *src) {
    __asm__ volatile(
        "lqc2 $vf16, 0(%1)\n"
        "vmulax.xyzw ACC, $vf12, $vf16x\n"
        "vmadday.xyzw ACC, $vf13, $vf16y\n"
        "vmaddz.xyzw $vf17, $vf14, $vf16z\n"
        "vaddw.xy $vf17, $vf17, $vf0w\n"
        "vmulw.xy $vf17, $vf17, $vf21w\n"
        "vaddw.z $vf17, $vf0, $vf0w\n"
        "sqc2 $vf17, 0(%0)\n"
        : : "r"(dst), "r"(src));
}
