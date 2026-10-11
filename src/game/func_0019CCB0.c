extern int D_00502170[][2];
void func_001CE9A0(int, int, int);

void func_0019CCB0(void) {
    int i;
    for (i = 0; i < 0x1A; i++) {
        if (D_00502170[i][0] > 0)
            func_001CE9A0(0, i + 100, 0);
    }
}
