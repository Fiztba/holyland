void func_00190A30(char *);
void func_00195898(char *, int);
void func_0019A310(char *, void (*)(void), int);
void func_001409D0(char *);
void func_00181378(void);
void func_001814B0(void);
void func_001816A0(void);

typedef struct {
    unsigned long long z0 : 32;
    unsigned long long b32 : 1;
    unsigned long long b33 : 1;
    unsigned long long z34 : 30;
} Flags;

typedef struct {
    char pad0[0x5C];
    int f5C;
    char pad60[0xD8 - 0x60];
    union { Flags b; unsigned long long l; } flags;
} Actor;

typedef struct {
    char pad0[0x7A];
    signed char c7a;
} Sub;

void func_00140650(char *arg0) {
    Actor *a = (Actor *)arg0;
    Sub *s = (Sub *)(arg0 + 0xE0);
    unsigned long long f;
    unsigned long long state;

    func_00190A30(arg0);
    a->f5C = 0;
    f = a->flags.l & ~0x200;
    a->flags.l = f;
    state = (f >> 36) & 3;
    if (state == 1) {
        if ((int)(f >> 19) & 1) {
            func_0019A310(arg0, func_00181378, 0);
        } else {
            func_0019A310(arg0, func_001816A0, 0);
        }
    } else if (state == 2) {
        func_0019A310(arg0, func_001816A0, 0);
    }
    func_00195898(arg0, 2);
    s->c7a = 1;
    a->flags.b.b32 = 1;
    a->flags.b.b33 = 1;
    func_001409D0(arg0);
}
