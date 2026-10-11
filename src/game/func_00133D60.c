void func_0019A310(char *, void (*)(void), int);
int func_0019A448(void *, void *, int, int, int, int, int);

typedef struct {
    char pad0[0xC6];
    unsigned short sC6;
    char padC8[0xCB - 0xC8];
    signed char cCB;
    char padCC[0xD8 - 0xCC];
    unsigned long long flags;
} Actor;

typedef struct {
    char pad0[0x80];
    unsigned char c80;
    unsigned char c81;
    short s82;
} Sub;

void func_00133D60(char *arg0) {
    Actor *a = (Actor *)arg0;
    Sub *s = (Sub *)(arg0 + 0xE0);
    int n;

    a->sC6 |= 2;
    switch (a->cCB) {
    case 0:
        n = s->s82 - 3;
        if (n < 0) {
            n = 0;
        }
        func_0019A448(arg0, &s->c80, 0, s->c80 & 0x1F, n, 5, 1);
        a->cCB++;
        a->flags = ((a->flags & ~0x3800ULL) | 0x800) & ~0x200ULL;
        break;
    case 1:
        if (s->c81 == 0) {
            a->sC6 &= ~2;
            func_0019A310(arg0, 0, 0);
        }
        break;
    }
}
