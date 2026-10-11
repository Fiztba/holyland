void func_001AF640(void *, int);
void func_001AF928(char *, int);
void func_001AF538(float, void *, void *);
void func_00216778(float *, void *);
int func_001C0E40(float, float *);

typedef struct {
    char pad[0x10];
    float f10;
    float f14;
    char c18;
} Sub;

void func_001A63C0(char *arg0, int arg1) {
    Sub *temp_s2;
    float sp[3];

    temp_s2 = (Sub *)(arg0 + 0x10);
    func_001AF640(arg0, arg1);
    func_001AF928(arg0, arg1);
    func_001AF538(*(float *)(*(char **)(arg0 + 0x54) + 0xD8), temp_s2, *(char **)(arg0 + 0x50) + 0xD0);
    temp_s2->f10 = *(float *)(*(char **)(arg0 + 0x54) + 0xDC);
    temp_s2->f14 = 0.0f;
    temp_s2->c18 = 0;
    func_00216778(sp, *(char **)(arg0 + 0x50) + 0xC0);
    sp[1] = sp[1] - temp_s2->f10 * 1.5f;
    if (func_001C0E40(temp_s2->f10 * 3.0f, sp) == 0) {
        temp_s2->c18 = 1;
    } else {
        *(float *)(*(char **)(arg0 + 0x50) + 0xC4) = *(float *)0x70000004;
    }
}
