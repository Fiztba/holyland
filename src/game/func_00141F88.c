typedef struct {
    char pad0[0xC6];
    unsigned short fC6;
    char padC8[4];
    signed char fCC;
    char padCD[0xA];
    unsigned char fD7;
    unsigned long b0 : 41;
    unsigned int b41 : 1;
    unsigned int b42 : 11;
    unsigned int b53 : 1;
    unsigned int b54 : 10;
} Actor;

extern unsigned int D_00446ED4;
void func_00142080(Actor *, int);
void func_00182DE0(Actor *, int);
void func_00182FD8(Actor *, int);
void func_0018E9C0(Actor *);
void func_00196870(Actor *, int);
void func_00196CC0(Actor *);

void func_00141F88(Actor *a, int arg1)
{
    if (D_00446ED4 % 10U == a->fD7 % 10U) {
        func_00196870(a, 1);
    }
    if (a->fC6 != 0) {
        return;
}
    func_00196CC0(a);
    func_0018E9C0(a);
    if (a->b53) {
        func_00182DE0(a, arg1);
        return;
    }
    switch (a->fCC) {
    case 0:
        break;
    case 1:
    case 2:
        func_00142080(a, arg1);
        return;
    }
        func_00182FD8(a, arg1);
}
