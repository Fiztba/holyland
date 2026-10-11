extern char *D_004471D4;

typedef struct {
    char b0;
    unsigned char b1;
    unsigned char b2;
    char b3;
} Slot;

int func_001CE8B0(unsigned int arg0) {
    unsigned char *p;
    int *ctl;
    Slot *s;
    int n;
    int m;

    if (*(unsigned short *)(D_004471D4 + 0x4D6) == 0) {
        return -1;
    }
    if (arg0 >= 0x7E) {
        return -1;
    }
    ctl = (int *)(*(int *)(D_004471D4 + 0x4CC) | 0x20000000);
    n = *ctl;
    if ((unsigned int)(n + 0xA) >= 0x3FC) {
        return -2;
    }
    m = n + 4;
    p = (unsigned char *)(((int)ctl + m) | 0x20000000);
    s = (Slot *)(D_004471D4 + (arg0 * 4 + 0x240));
    *p++ = 0xFF;
    *p++ = ((unsigned char *)(D_004471D4 + (s->b0 << 5)))[0x18];
    *p++ = 7;
    *p++ = 0xFD;
    *p++ = 0x10;
    *p++ = s->b1;
    *p++ = s->b2;
    *p++ = arg0;
    p[1] = p[0] = 0;
    *ctl += 0xA;
    s->b3 = -1;
    return 0;
}
