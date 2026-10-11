void *func_001267B0(int);
float func_0019B838(float);

void func_002165D0(char *arg0) {
    char *temp_v0;

    temp_v0 = func_001267B0(*(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        *(float *)(*(char **)temp_v0 + 0xD0) = *(float *)(arg0 + 0x10);
        *(float *)(*(char **)temp_v0 + 0xD4) = *(float *)(arg0 + 0x18);
        *(float *)(*(char **)temp_v0 + 0xD8) = *(float *)(arg0 + 0x20);
        *(float *)(*(char **)temp_v0 + 0xD0) = func_0019B838(*(float *)(*(char **)temp_v0 + 0xD0));
        *(float *)(*(char **)temp_v0 + 0xD4) = func_0019B838(*(float *)(*(char **)temp_v0 + 0xD4));
        *(float *)(*(char **)temp_v0 + 0xD8) = func_0019B838(*(float *)(*(char **)temp_v0 + 0xD8));
    }
    *(int *)arg0 = 0;
}
