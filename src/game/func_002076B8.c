void func_002076B8(void *p) {
    __asm__ volatile(
        "lqc2 $vf12, 0x0(%0)\n"
        "lqc2 $vf13, 0x10(%0)\n"
        "lqc2 $vf14, 0x20(%0)\n"
        "lqc2 $vf15, 0x30(%0)\n"
        : : "r"(p));
}
