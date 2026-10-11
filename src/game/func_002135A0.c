typedef struct {
    int unk0;
    char unk4[4];
    unsigned int unk8;
    char unkC[4];
    int unk10;
} Arg;

extern unsigned char D_00369830[];
extern unsigned char D_0041A6F8[];

void func_001DC120(unsigned int, unsigned int, int);

void func_002135A0(Arg *arg0) {
    unsigned int idx;
    int add;
    int v;
    int t;
    unsigned char *e;
    unsigned char *p;

    add = arg0->unk10;
    idx = arg0->unk8;
    if (idx < 0x17D) {
        p = &D_00369830[idx];
        v = *p + add;
        if (v >= 0x64) {
            *p = 0x63;
        } else if (v < 0) {
            *p = 0;
        } else {
            *p = v;
        }
        if ((int)idx < 0x96) {
            t = idx << 5;
            e = D_0041A6F8 + t;
            if (*(unsigned short *)(e + 0xE) >> 1 & 1) {
                if (D_00369830[idx] >= 2) {
                    D_00369830[idx] = 1;
                }
            }
            if (D_0041A6F8[t] == 3) {
                func_001DC120(idx, idx, t);
            }
        } else {
            if (D_00369830[idx] >= 2) {
                D_00369830[idx] = 1;
            }
        }
    }
    arg0->unk0 = 0;
}
