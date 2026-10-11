typedef struct {
    unsigned long X : 16;
    unsigned long Y : 16;
    unsigned long Z : 32;
} Xyz;

typedef struct {
    Xyz a;
    long pad8;
    Xyz b;
    char pad18[0x3F - 0x18];
    signed char f3F;
    char pad40[0x60 - 0x40];
} S;

extern S D_005DD460;

S *func_00204E50(S *arg0, int arg1) {
    if (arg0->f3F == arg1) {
        D_005DD460 = *arg0;
        D_005DD460.a.X -= 0xF0;
        D_005DD460.b.X -= 0xF0;
        return &D_005DD460;
    }
    return arg0;
}
