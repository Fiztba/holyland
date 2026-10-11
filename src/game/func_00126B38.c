typedef struct {
    char pad0[0xC6];
    unsigned short c6;
    char pad1[0xCC - 0xC8];
    char cc;
    char pad2[0xD7 - 0xCD];
    unsigned char d7;
    unsigned long d8;
} Actor;

extern unsigned int D_00446ED4;

void func_00126C28(void *, int);
void func_00126C70(void *, int);
void func_00126E58(void *, int);
void func_0018E9C0(void *);
void func_00196870(void *, int);
void func_00196CC0(void *);

void func_00126B38(void *arg0, int arg1) {
    Actor *a = arg0;
    unsigned int temp_hi;
    unsigned char temp_a2;
    unsigned long bit;

    temp_hi = D_00446ED4 % 10U;
    temp_a2 = a->d7;
    if (temp_hi == temp_a2 % 10U) {
        func_00196870(arg0, 0);
    }
    if (a->c6 != 0) {
        return;
    }
    func_00196CC0(arg0);
    func_0018E9C0(arg0);
    bit = (a->d8 >> 0x35) & 1;
    if (bit) {
        func_00126C28(arg0, arg1);
        return;
    }
    if (a->cc == 2) {
        func_00126E58(arg0, arg1);
        return;
    }
    func_00126C70(arg0, arg1);
}
