typedef struct {
    unsigned long X : 16;
    unsigned long Y : 16;
    unsigned long Z : 32;
} Xyz;

typedef struct {
    Xyz a;
    long pad8;
    Xyz b;
    float f18;
    char pad1C[0x3F - 0x1C];
    signed char f3F;
    char pad40[0x60 - 0x40];
} S;

extern S D_005DDA00;
extern unsigned char D_00349681[];
extern unsigned char D_00349682[];
extern unsigned char D_00349683[];

S *func_00205F38(S *arg0) {
    unsigned char v;

    D_005DDA00 = *arg0;
    switch (arg0->f3F) {
    case 0:
        v = D_00349681[0];
        break;
    case 1:
        v = D_00349682[0];
        break;
    case 2:
        v = D_00349683[0];
        break;
    default:
        v = 0x7F;
        break;
    }
    D_005DDA00.b.X = D_005DDA00.a.X + v * 16;
    D_005DDA00.f18 = (float)(v * 2) / 255.0f;
    return &D_005DDA00;
}
