void func_001AF928(char *, int);
void func_001AF640(void *, int);

void func_001AA8E0(char *arg0, int arg1) {
    char *temp_s0;
    char *temp_v0;

    temp_s0 = arg0 + 0x10;
    func_001AF928(arg0, arg1);
    func_001AF640(arg0, arg1);
    temp_v0 = *(char **)(arg0 + 0x54);
    *(float *)(arg0 + 0x10) = *(float *)(temp_v0 + 0xDC);
    *(float *)(temp_s0 + 0x4) = *(float *)(temp_v0 + 0xE0);
}
