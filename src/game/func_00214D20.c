int func_00211D90(void *, double);
extern float D_0042C6F0[];

void func_00214D20(char *arg0) {
    char *p = arg0 + 0xC;
    int n = (int)(*(float *)(arg0 + 0x8) * 4096.0f / 6.2831855f);
    int i = n + 0x1000;
    int idx = i % 4096;

    func_00211D90(p, D_0042C6F0[idx]);
    *(int *)arg0 = 0;
}
