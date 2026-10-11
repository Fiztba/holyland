int func_001CBAE0();
extern int D_005BF280[];

void func_001CF168(void *arg0, int arg1) {
    unsigned char *out;
    int *src;
    int i;

    func_001CBAE0(1, 0x1090, arg1);
    out = arg0;
    src = D_005BF280;
    i = 3;
    do {
        out[0] = *((unsigned char *)src);
        out[1] = *(((unsigned char *)src) + 1);
        out[2] = (((unsigned int)(*src)) & 0xFF0000) >> 16;
        out[3] = (((unsigned int)(*src)) & 0xFF000000) >> 24;
        src++;
        out += 4;
        i--;
    } while (i >= 0);
}
