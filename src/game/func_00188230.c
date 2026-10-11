void func_00188230(void *a, void *b, void *c, float r) {
    float tmp[4];
    float t;
    float d;

    __asm__ volatile(
        "lui $3, 0x7000\n"
        "lqc2 $vf16, 0(%2)\n"
        "lqc2 $vf17, 0(%3)\n"
        "lqc2 $vf18, 0(%4)\n"
        "vsub.xyz $vf19, $vf17, $vf16\n"
        "vsub.xyz $vf20, $vf18, $vf16\n"
        "vmul.xyz $vf20, $vf19, $vf20\n"
        "vaddy.x $vf20, $vf20, $vf20y\n"
        "vaddz.x $vf20, $vf20, $vf20z\n"
        "vmul.xyz $vf21, $vf19, $vf19\n"
        "vaddy.x $vf21, $vf21, $vf21y\n"
        "vaddz.x $vf21, $vf21, $vf21z\n"
        "vdiv Q, $vf20x, $vf21x\n"
        "sqc2 $vf19, %1\n"
        "vwaitq\n"
        "vaddq.x $vf20, $vf0, Q\n"
        "qmfc2 $2, $vf20\n"
        "mtc1 $2, %0\n"
        : "=f"(t), "=m"(tmp)
        : "r"(a), "r"(b), "r"(c)
        : "$2", "$3");

    if (0.0f <= t && t <= 1.0f) {
        __asm__ volatile(
            "lqc2 $vf16, 0(%2)\n"
            "lqc2 $vf17, 0(%3)\n"
            "lqc2 $vf18, 0($29)\n"
            "mfc1 $2, %1\n"
            "qmtc2 $2, $vf20\n"
            "vmulx.xyzw $vf19, $vf18, $vf20x\n"
            "vadd.xyz $vf19, $vf19, $vf16\n"
            "vsub.xyz $vf20, $vf17, $vf19\n"
            "vmul.xyz $vf20, $vf20, $vf20\n"
            "vaddy.x $vf20, $vf20, $vf20y\n"
            "vaddz.x $vf20, $vf20, $vf20z\n"
            "vsqrt Q, $vf20x\n"
            "sqc2 $vf19, 0($3)\n"
            "vwaitq\n"
            "vaddq.x $vf20, $vf0, Q\n"
            "qmfc2 $2, $vf20\n"
            "mtc1 $2, %0\n"
            : "=f"(d)
            : "f"(t), "r"(a), "r"(c)
            : "$2", "$3");

        *(float *)0x70000014 = d;
        if (!(r <= d)) {
            __asm__ volatile(
                "lqc2 $vf16, 0(%0)\n"
                "lqc2 $vf17, 0($3)\n"
                "vsub.xyz $vf18, $vf16, $vf17\n"
                "vmul.xyz $vf19, $vf18, $vf18\n"
                "vaddy.x $vf19, $vf19, $vf19y\n"
                "vaddz.x $vf19, $vf19, $vf19z\n"
                "vsqrt Q, $vf19x\n"
                "vwaitq\n"
                "vaddq.x $vf19, $vf0, Q\n"
                "vdiv Q, $vf0w, $vf19x\n"
                "vwaitq\n"
                "vmulq.xyz $vf18, $vf18, Q\n"
                "sqc2 $vf18, 0(%1)\n"
                : : "r"(c), "r"((void *)0x70000020));

            *(int *)0x70000010 = 1;
        }
    }
}
