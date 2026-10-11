extern char D_0038CD20[];

void func_00119E10(int);

void func_00119520(void) {
    int i;
    unsigned char *p;

    for (i = 0; i < 2; i++) {
        p = (unsigned char *)(D_0038CD20 + i * 0x60);
        if (p[0] != 0 && p[1] == 0x1B) {
            func_00119E10(i);
        }
    }
}
