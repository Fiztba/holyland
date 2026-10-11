int func_001CBAE0();
extern char *D_004471D4;

int func_001CD578(int arg0, int arg1) {
    char *temp_s0;

    temp_s0 = D_004471D4 + (arg0 << 5);
    *(char *)(temp_s0 + 0x1A) = arg1;
    return ((func_001CBAE0(1, 0x90, (char)arg1, 0, *(int *)(temp_s0 + 0x8), *(int *)(temp_s0 + 0xC), *(int *)(temp_s0 + 0x14)) ^ *(int *)(temp_s0 + 0x14)) == 0) ? 0 : -1;
}
