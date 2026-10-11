extern char D_0038CD20[];
extern int D_00446770;
void func_0010A4E8(unsigned char, unsigned char, unsigned char, unsigned char, int);

void func_00116AA0(void) {
    unsigned char *p;
    char *base;

    base = D_0038CD20 + D_00446770 * 0x60;
    p = *(unsigned char **)(base + 8);
    func_0010A4E8(p[0x78], p[0x79], p[0x7A], p[0x7B], 0x44);
}
