void func_001AF928(char *, int);
void func_001AF640(void *, int);
void func_00216C48(void *, void *, void *);
void func_00216778(void *, void *);
void func_001C1430(float, void *, void *);
float func_001B0178(void *, void *);
void func_001AF538(float, void *, void *);

void func_001ABEF8(char *arg0, int arg1) {
    int vec[4];
    char *temp_s2 = arg0 + 0x10;

    func_001AF928(arg0, arg1);
    func_001AF640(arg0, arg1);
    *(float *)(temp_s2 + 0x20) = 0;
    func_001AF538(1000.0f, vec, *(char **)(arg0 + 0x50) + 0xD0);
    func_001C1430(1.0f, *(char **)(arg0 + 0x50) + 0xC0, vec);
    if (*(int *)0x70000010 != 0) {
        func_00216778(arg0 + 0x20, (void *)0x70000000);
    } else {
        func_00216C48(arg0 + 0x20, vec, *(char **)(arg0 + 0x50) + 0xC0);
    }
    *(float *)(temp_s2 + 0x20) = func_001B0178(temp_s2 + 0x10, *(char **)(arg0 + 0x50) + 0xC0);
    func_001AF538(100.0f, temp_s2, *(char **)(arg0 + 0x50) + 0xD0);
}
