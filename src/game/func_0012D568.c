typedef struct {
    char pad0[0xC6];
    unsigned short flags;          /* 0xC6 */
    char pad1[0xCC - 0xC8];
    signed char fCC;               /* 0xCC */
    char pad2[0xD7 - 0xCD];
    unsigned char byteD7;          /* 0xD7 */
    unsigned long long bits;       /* 0xD8 */
} Actor;

extern unsigned int D_00446ED4;

int func_0012D8A0(void *, int);
int func_0012D9A0(void *, int);
int func_0012D668(void *, int);
void func_0018E9C0(void *);
void func_00196870(void *, int);
int func_00196C28(void *);

void func_0012D568(void *arg0, int arg1) {
    Actor *a = arg0;
    unsigned int temp_hi;

    temp_hi = D_00446ED4 % 10U;
    if (temp_hi == a->byteD7 % 10U) {
        func_00196870(arg0, 1);
    }
    if (a->flags != 0) {
        return;
    }
    func_00196C28(arg0);
    func_0018E9C0(arg0);
    if ((int) (a->bits >> 0x35) & 1) {
        func_0012D668(arg0, arg1);
        return;
    }
    switch (a->fCC) {
    case 0:
    case 1:
        break;
    case 2:
        func_0012D9A0(arg0, arg1);
        return;
    }
    func_0012D8A0(arg0, arg1);
}
