extern char D_01E0DB00[];
int scePadInit(int);

int func_00216F50(int arg0) {
    unsigned char *p;
    unsigned char *end;

    D_01E0DB00[0xE00] = 0;
    D_01E0DB00[0xE01] = 0;
    p = (unsigned char *) D_01E0DB00;
    end = (unsigned char *) (D_01E0DB00 + 0xE00);
    do {
        *(short *)(p + 0x120) = 0;
        *(short *)(p + 0x122) = 0;
        *(short *)(p + 0x164) = 0;
        p[0x17A] = 0;
        p[0x17C] = 0;
        p[0x17E] = 0xFF;
        p[0x17F] = 0xFF;
        p[0x180] = 0;
        p += 0x1C0;
    } while ((int) p < (int) end);
    return scePadInit(arg0);
}
