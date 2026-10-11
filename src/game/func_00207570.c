void func_00207570(char *arg0, char *arg1) {
    __asm__ volatile(
        "lqc2 $vf16, 0x0(%0)\n"
        "vmulax.xyzw ACC, $vf1, $vf16x\n"
        "vmadday.xyzw ACC, $vf2, $vf16y\n"
        "vmaddaz.xyzw ACC, $vf3, $vf16z\n"
        "vmaddw.xyzw $vf17, $vf4, $vf16w\n"
        "vnop\n"
        "vnop\n"
        "vnop\n"
        "vdiv Q, $vf0w, $vf17w\n"
        "vwaitq\n"
        "vmulq.xyzw $vf26, $vf17, Q\n"
        "vnop\n"
        "vnop\n"
        "vnop\n"
        "vftoi4.xyzw $vf17, $vf26\n"
        "vnop\n"
        "vnop\n"
        "vnop\n"
        "sqc2 $vf17, 0x0(%1)"
        : : "r"(arg0), "r"(arg1));
}
