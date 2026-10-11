typedef struct __attribute__((packed)) { unsigned long long a, b; } Unal;
typedef unsigned int u128 __attribute__((mode(TI)));
typedef struct { char pad[0x78]; unsigned long long w; } Ent;

void func_001A2E58(char *arg0, int unused, int arg2, int arg3) {
    char *ent;
    int i;
    unsigned short total = *(unsigned short *)(arg0 + 0xF0);

    *(int *)(arg0 + 0x17C) = 0;
    *(u128 *)(arg0 + 0x180) = 0;
    ent = *(char **)(arg0 + 0xF8);
    for (i = 0; i < total; i++, ent += 0x80) {
        int n = (int)(((Ent *)ent)->w >> 53);
        char *s = *(char **)(ent + 0x78);
        int j;
        for (j = 0; j < n; j++, s += 0x10) {
            if ((*(int *)(s + 0x40) & 0x3FFF) == arg2) {
                *(Unal *)(arg0 + 0x180) = *(Unal *)(s + 0x40);
                *(int *)(arg0 + 0x17C) = arg2;
                *(int *)(arg0 + 0x180) = (*(int *)(arg0 + 0x180) & 0xFFFFC000) | arg3;
                return;
            }
            s += 0x60;
            {
                int hdr = *(int *)s;
                s += 0x10;
                s += (hdr & 0x7FFF) << 6;
            }
        }
    }
}
