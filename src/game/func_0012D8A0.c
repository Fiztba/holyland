int func_0018EAC0(void *, void *);
short func_00190D50(float, void *, int);
short func_00191340(void *, int);
void func_001958B8(char *, void (*)(void), int);
void func_0018B6F8(void *, float, float, void *, int);
int rand(void);

typedef struct {
    char pad0[0x4C];
    unsigned short s4C;
    char pad4E[0x56 - 0x4E];
    unsigned short s56;
} Src;

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
    int p0;
    char pad4[0x10 - 0x4];
    Src *p10;
    char pad14[0x5C - 0x14];
    char *p5C;
    char pad60[0xA0 - 0x60];
    unsigned short sA0;
    char padA2[0xC8 - 0xA2];
    short sC8;
    char padCA[0xD8 - 0xCA];
    union { Flags b; unsigned long l; } flags;
    Sub sub;
} Actor;

void func_0012D8A0(Actor *arg0, int arg1) {
    Sub *s = &arg0->sub;
    long buf;
    int temp_v0_2;
    char *s0;
    Src *src;

    arg0->flags.b.f11 = 0;
    if (arg0->sA0 == 0) {
        src = arg0->p10;
        s0 = (char *)arg0 + 0x110;
        func_0018B6F8(s0, (float) src->s4C, (float) src->s56, src, arg0->p0 + 0xC0);
        arg0->p5C = s0;
        arg0->sA0 = (unsigned short) ((rand() % 60) + 0x78);
    }
    temp_v0_2 = func_0018EAC0(arg0, &buf);
    if (temp_v0_2 != 0) {
        arg0->sC8 = func_00191340(arg0, temp_v0_2);
    } else {
        arg0->sC8 = func_00190D50(50.0f, arg0, 0);
    }
    func_001958B8((char *) arg0, s->f18, arg1);
}
