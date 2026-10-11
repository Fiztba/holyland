typedef struct {
    char pad0[0xC];
    int *ctrl;                     /* 0xC */
    char pad1[0xC6 - 0x10];
    unsigned short flags;          /* 0xC6 */
    char pad2[0xD7 - 0xC8];
    unsigned char byteD7;          /* 0xD7 */
    unsigned long long bits;       /* 0xD8 */
} Actor;

extern unsigned int D_00446ED4;

int func_0014D170(void *, int);
int func_00182DE0(void *, int);
void func_0018E9C0(void *);
int func_00196870(void *, int, unsigned char, unsigned int);
int func_00196CC0(void *);

void func_0014D090(void *arg0, int arg1) {
    Actor *a = arg0;
    unsigned int temp_hi;

    temp_hi = D_00446ED4 % 10U;
    if (temp_hi == a->byteD7 % 10U) {
        func_00196870(arg0, 1, a->byteD7, temp_hi);
    }
    if (a->flags != 0) {
        return;
    }
    if ((*a->ctrl & 0x06000000) != 0x06000000) {
        func_00196CC0(arg0);
    }
    func_0018E9C0(arg0);
    if ((int) (a->bits >> 0x35) & 1) {
        func_00182DE0(arg0, arg1);
        return;
    }
    func_0014D170(arg0, arg1);
}
