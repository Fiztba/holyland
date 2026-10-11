extern char *D_004471D4;

int func_001CEB38(int arg0, char arg1, char arg2) {
    int *ctl;
    int n;
    int m;
    unsigned char *p;

    if (*(unsigned short *)(D_004471D4 + 0x4D6) == 0) {
        return -1;
    }
    ctl = (int *)(*(int *)(D_004471D4 + 0x4CC) | 0x20000000);
    n = *ctl;
    if ((unsigned int)(n + 5) >= 0x3FC) {
        return -2;
    }
    m = n + 4;
    p = (unsigned char *)(((int)ctl + m) | 0x20000000);
    *p++ = 0xFF;
    *p++ = ((unsigned char *)(D_004471D4 + (arg0 << 5)))[0x18];
    *p++ = 2;
    p[0] = (char)(arg1 - 0x40);
    p[1] = arg2;
    *ctl += 5;
    return 0;
}
