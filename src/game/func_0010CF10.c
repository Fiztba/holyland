typedef struct { char pad[0x78]; unsigned long long w; } Ent;
void func_0010CF10(char *o) {
    int i = 0;
    unsigned short total = *(unsigned short *)(o + 0xF0);
    Ent *e = *(Ent **)(o + 0xF8);
    long long *s = *(long long **)(o + 0x1A4);
    for (; i < total; i++, e++) {
        int j = 0;
        int n = (int)(e->w >> 53);
        char *p = *(char **)&e->w;
        for (; j < n; j++) {
            char *cur = p;
            unsigned short u = *(unsigned short *)p;
            int m;
            int k = 0;
            char *d;
            p += 0x30;
            *(long long *)p = *s++;
            m = *(int *)(cur + 0x90) & 0x7FFF;
            d = cur + 0xB0;
            p += 0x10;
            *(long long *)p = *s++;
            p += 0x10;
            *(long long *)p = *s++;
            p += 0x10;
            *(long long *)p = *s++;
            for (; k < m; k++) {
                *(long long *)(d + 0x20) = *s++;
                d += 0x40;
            }
            p = cur + (u + 1) * 16;
        }
    }
}
