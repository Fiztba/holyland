extern char D_00428B80[];

int func_001FBC80(int arg0) {
    char *p = D_00428B80 + arg0 * 0x28;

    if (p[0x20] != 0) {
        return 1;
    } else {
        if (p[0x21] != 2) {
            return 2;
        }
        if (p[2] == 0 || *(int *)(p + 0x18) >= 0x96 || p[3] != 0) {
            return 0;
        }
        return 8;
    }
}
