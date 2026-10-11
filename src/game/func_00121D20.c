void func_00121D20(char *arg0, int arg1) {
    char *s;
    int *p;
    int temp_a0;

    s = arg0;
    p = (int *)(s + 0x40);
    temp_a0 = *(int *)(s + 0x10);
    if ((temp_a0 & 0x2FF00) == 0x100) {
        if (arg1 != 0) {
            *(int *)(s + 0x40) = 0;
            *(int *)(s + 0x10) = temp_a0 | 0x20000;
            *(char *)(s + 0x11) = 2;
            return;
        }
        goto block_4;
    }
block_4:
    if (((temp_a0 & 0x2FF00) == 0x20300) && (arg1 == 0)) {
        *p = 0;
        *(int *)(s + 0x10) = *(int *)(s + 0x10) & 0xFFFDFFFF;
        *(char *)(s + 0x11) = 4;
    }
}
