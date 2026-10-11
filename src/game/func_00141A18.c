int func_0018EAC0(void *, void *, int);
short func_00190F90(float, void *, int);
void func_001958B8(char *, void (*)(void), int);

typedef struct {
    char pad0[0x18];
    void (*f18)(void);
} Sub;

typedef struct {
    char pad0[0x10];
    unsigned short s10;
} Ctl;

typedef struct {
    unsigned long long z0 : 9;
    unsigned long long b9 : 1;
    unsigned long long z10 : 1;
    unsigned long long f11 : 3;
    unsigned long long z14 : 3;
    unsigned long long b17 : 1;
    unsigned long long z18 : 10;
    unsigned long long b28 : 1;
    unsigned long long z29 : 35;
} Flags;

typedef struct {
    char pad0[0xC];
    Ctl *c;
    char pad10[0xC8 - 0x10];
    short sC8;
    char padCA[0xD8 - 0xCA];
    union { Flags b; unsigned long l; } flags;
    Sub sub;
} Actor;

void func_00141A18(Actor *arg0, int arg1) {
    Sub *s = &arg0->sub;
    long buf;
    int temp_v0;
    void (*fn)(void);

    if (arg0->c->s10 & 0x100) {
        arg0->flags.b.f11 = 0;
    } else {
        arg0->flags.b.f11 = 1;
    }
    arg0->flags.b.z10 = 0;
    arg0->flags.b.b9 = 0;
    temp_v0 = func_0018EAC0(arg0, &buf, -0x201);
    if (temp_v0 > 0) {
        arg0->sC8 = 0x201;
    } else if (temp_v0 < 0) {
        arg0->sC8 = 0x101;
    } else {
        arg0->sC8 = func_00190F90(50.0f, arg0, 0);
    }
    fn = s->f18;
    func_001958B8((char *)arg0, fn, arg1);
}
