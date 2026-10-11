void func_00216D20(void *dst, void *src) {
    __asm__ volatile(
        "lqc2 $vf16, 0(%1)\n"
        "vmul.xyz $vf17, $vf16, $vf16\n"
        "vaddx.z $vf17, $vf17, $vf17x\n"
        "vaddy.z $vf17, $vf17, $vf17y\n"
        "vsqrt Q, $vf17z\n"
        "vwaitq\n"
        "vmulq.xyzw $vf17, $vf0, Q\n"
        "vdiv Q, $vf0w, $vf17w\n"
        "vwaitq\n"
        "vmulq.xyz $vf16, $vf16, Q\n"
        "sqc2 $vf16, 0(%0)\n"
        : : "r"(dst), "r"(src) : "memory");
}
