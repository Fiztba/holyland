void func_001A3E30(char *);
int rand(void);

void func_001A4078(char *arg0) {
    char *p = arg0 + 0x10;
    func_001A3E30(arg0);
    *(float *)(p + 0x18) = (float)(((float)(rand() & 0xFFFF) * 0.000030517578f - 1.0f) * *(float *)(*(char **)(arg0 + 0x54) + 0xDC)) + 1.0f;
}
