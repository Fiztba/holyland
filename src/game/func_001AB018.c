void func_001AF928(char *, int);
void func_001AF640(void *, int);
void func_001AF538(float, void *, void *);

void func_001AB018(char *arg0, int arg1) {
    char *temp_s2;

    temp_s2 = arg0 + 0x10;
    func_001AF928(arg0, arg1);
    func_001AF640(arg0, arg1);
    func_001AF538(*(float *)(*(char **)(arg0 + 0x54) + 0xD8), temp_s2, *(char **)(arg0 + 0x50) + 0xD0);
    *(char *)(temp_s2 + 0x10) = 0;
}
