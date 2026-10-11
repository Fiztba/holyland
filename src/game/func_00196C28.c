void func_00196C28(char *arg0) {
    char *temp_v0;
    char *temp_v1;
    float temp_f0;
    float temp_f1;
    float temp_f2;
    unsigned short temp_v0_2;

    temp_v1 = *(char **)(arg0 + 0x10);
    temp_v0 = *(char **)arg0;
    temp_f0 = *(float *)(temp_v0 + 0xC0) - *(float *)temp_v1;
    temp_f2 = *(float *)(temp_v0 + 0xC4) - *(float *)(temp_v1 + 4);
    temp_v0_2 = *(unsigned short *)(temp_v1 + 0x4C);
    temp_f1 = *(float *)(temp_v0 + 0xC8) - *(float *)(temp_v1 + 8);
    if ((float)(temp_v0_2 * temp_v0_2) < (temp_f0 * temp_f0) + (temp_f2 * temp_f2) + (temp_f1 * temp_f1)) {
        *(unsigned long *)(arg0 + 0xD8) |= (0x8000UL << 38);
        return;
    }
    *(unsigned long *)(arg0 + 0xD8) &= 0xFFDFFFFFFFFFFFFFUL;
}
