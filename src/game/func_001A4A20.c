extern char D_00413F80[];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float, void *, void *);
int func_001AFD08(void *, void *, void *);

void func_001A4A20(char *arg0, int arg1) {
    char *temp_s2;
    char *q;

    temp_s2 = arg0 + 0x10;
    func_001AF928(arg0, arg1);
    func_001AF640(arg0, arg1);
    q = *(char **)(arg0 + 0x50);
    *(float *)(q + 0xC4) = *(float *)(q + 0xC4) - 220.0f;
    func_001AF538(*(float *)(*(char **)(arg0 + 0x54) + 0xD8), temp_s2, q + 0xD0);
    *(int *)(arg0 + 0x6C) = func_001AFD08(arg0, D_00413F80, D_00413F80 + 0x10);
    *(float *)(temp_s2 + 0x10) = *(float *)(*(char **)(arg0 + 0x54) + 0xDC);
}
