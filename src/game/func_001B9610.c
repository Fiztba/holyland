extern unsigned char D_00413F80[];
extern int D_0038CC50[];
int func_0019D0F0(int);

void func_001B9610(void)
{
    unsigned char *s = D_00413F80;

    if (s[0x1A8] == 0) {
        if (*(short *)(s + 0x1A4) > 0) {
            return;
        }
        if (*(int *)(s + 0xB4) > 0) {
            return;
        }
        s[0x1A8] = 1;
        func_0019D0F0(0x28);
    }
    if (s[0x1A8] < 0x3C) {
        *(float *)(s + 0x10) += (1.012291f - *(float *)(s + 0x10)) / (0x3D - s[0x1A8]);
        s[0x1A8]++;
    } else {
        D_0038CC50[9] |= 0x200;
    }
}
