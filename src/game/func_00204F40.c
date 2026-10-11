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

extern S D_005DD520;

S *func_00204F40(S *arg0, int arg1) {
    if (arg0->f3F != arg1) {
        D_005DD520 = *arg0;
        D_005DD520.a.X -= 0xF0;
        D_005DD520.b.X -= 0xF0;
        return &D_005DD520;
    }
    return 0;
}
