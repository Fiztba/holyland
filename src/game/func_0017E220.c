extern int D_00413F80[];

int func_0018EAC0(void *, void *);
short func_0017EA98(float, void *, int);
void func_001958B8(char *, void (*)(void), int);

typedef struct {
    char pad0[0x18];
    void (*f18)(void);
} Sub;

typedef struct {
    unsigned long long z0 : 9;
    unsigned long long b9 : 1;
    unsigned long long z10 : 1;
    unsigned long long f11 : 3;
    unsigned long long z14 : 50;
} Flags;

typedef struct {
    char pad0[0x5C];
    int *p5C;
    char pad60[0xA0 - 0x60];
    unsigned short sA0;
    char padA2[0xC8 - 0xA2];
    short sC8;
    char padCA[0xD8 - 0xCA];
    union { Flags b; unsigned long l; } flags;
    Sub sub;
} Actor;

void func_0017E220(Actor *arg0, int arg1) {
    Sub *s = &arg0->sub;
    long buf;
    int temp_v0;

    arg0->flags.b.f11 = 0;
    arg0->flags.b.z10 = 0;
    arg0->flags.b.b9 = 0;
    if (arg0->sA0 == 0) {
        arg0->p5C = D_00413F80;
    }
    temp_v0 = func_0018EAC0(arg0, &buf);
    if (temp_v0 > 0) {
        arg0->sC8 = 0x201;
    } else if (temp_v0 < 0) {
        arg0->sC8 = 0x101;
    } else {
        arg0->sC8 = func_0017EA98(50.0f, arg0, 0);
    }
    func_001958B8((char *)arg0, s->f18, arg1);
}
