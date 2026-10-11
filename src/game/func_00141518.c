typedef struct {
    char pad0[0x7A];
    signed char cnt;
} Sub;

typedef struct {
    char pad0[0x5C];
    int f5C;
    char pad60[0x40];
    short fA0;
    char padA2[0x24];
    unsigned short fC6;
    short fC8;
    char padCA[2];
    signed char fCC;
    char padCD[0xA];
    unsigned char fD7;
    unsigned long b0 : 41;
    unsigned int b41 : 1;
    unsigned int b42 : 11;
    unsigned int b53 : 1;
    unsigned int b54 : 10;
    Sub sub;
} Actor;

extern unsigned int D_00446ED4;
void func_00141C38(void);
void func_001416B8(Actor *, int);
void func_00141A18(Actor *, int);
void func_00141AF0(Actor *, int);
void func_00182DE0(Actor *, int);
void func_0018E9C0(Actor *);
void func_001958B8(Actor *, void (*)(void), int);
void func_00196870(Actor *, int);
void func_00196CC0(Actor *);

void func_00141518(Actor *a, int arg1)
{
    Sub *s = &a->sub;

    if (D_00446ED4 % 10U == a->fD7 % 10U) {
        func_00196870(a, 1);
    }
    if (a->b41 == 1) {
        s->cnt++;
    }
    if (a->fC6 != 0) {
        return;
    }
    func_00196CC0(a);
    func_0018E9C0(a);
    if (s->cnt >= 10) {
        a->fC6 = 0;
        a->fC8 = 0;
        a->fA0 = 0;
        a->f5C = 0;
        s->cnt = 0;
        func_001958B8(a, func_00141C38, arg1);
        return;
    } else if (a->b53) {
        func_00182DE0(a, arg1);
    } else {
        switch (a->fCC) {
        case 0:
            func_00141A18(a, arg1);
            break;
        case 1:
            func_00141AF0(a, arg1);
            break;
        case 2:
            func_001416B8(a, arg1);
            break;
        default:
            func_00141A18(a, arg1);
            break;
        }
    }
}
