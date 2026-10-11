typedef struct {
    char pad0[0xC6];
    unsigned short flags;          /* 0xC6 */
    char pad1[0xD7 - 0xC8];
    unsigned char byteD7;          /* 0xD7 */
} Actor;

extern unsigned int D_00446ED4;

int func_00147B98(void *, int);
void func_0018E9C0(void *);
void func_00196870(void *, int);

void func_00147B00(void *arg0, int arg1) {
    Actor *a = arg0;
    unsigned int temp_hi;

    temp_hi = D_00446ED4 % 10U;
    if (temp_hi == a->byteD7 % 10U) {
        func_00196870(arg0, 0);
    }
    if (a->flags == 0) {
        func_0018E9C0(arg0);
        func_00147B98(arg0, arg1);
    }
}
