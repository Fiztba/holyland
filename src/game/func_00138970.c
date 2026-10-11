int func_00192C98(void *, int, int, unsigned int, int);

typedef struct {
    char pad0[0x1C];
    int f1C;
} Sub;

typedef struct {
    char pad0[0xC];
    struct {
        char pad0[0xC4];
        unsigned char c4;
    } *c;
    char pad10[0xC8];
    unsigned long b0 : 18;
    unsigned long b18 : 1;
    unsigned long b19 : 45;
    Sub sub;
} Actor;

void func_00138970(Actor *a) {
    Sub *s = &a->sub;

    func_00192C98(a, 1, 0, a->c->c4, 0);
    s->f1C = 0;
    a->b18 = 1;
}
