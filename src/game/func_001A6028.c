void func_001AF640(void *, int);
void func_001AF928(char *, int);
void func_001AF538(float, void *, void *);
void func_00216778(float *, void *);
int func_001C0E40(float, float *);

typedef struct {
    char pad[0xD8];
    float fD8, fDC, fE0, fE4;
} Ent;

typedef struct {
    char pad[0x10];
    float f10;
    float f14;
    float f18;
    float f1C;
    float f20;
    char c24;
} Sub;

void func_001A6028(char *arg0, int arg1) {
    Sub *temp_s2;
    float sp[3];

    temp_s2 = (Sub *)(arg0 + 0x10);
    func_001AF640(arg0, arg1);
    func_001AF928(arg0, arg1);
    func_001AF538(*(float *)(*(char **)(arg0 + 0x54) + 0xD8), temp_s2, *(char **)(arg0 + 0x50) + 0xD0);
    temp_s2->f10 = *(float *)(*(char **)(arg0 + 0x54) + 0xDC);
    temp_s2->f14 = 0.0f;
    temp_s2->f18 = *(float *)(*(char **)(arg0 + 0x54) + 0xE0);
    temp_s2->f1C = *(float *)(*(char **)(arg0 + 0x54) + 0xE4);
    temp_s2->f20 = 1.57f;
    temp_s2->c24 = 0;
    func_00216778(sp, *(char **)(arg0 + 0x50) + 0xC0);
    sp[1] = sp[1] - temp_s2->f10 * 1.5f;
    if (func_001C0E40(temp_s2->f10 * 3.0f, sp) == 0) {
        temp_s2->c24 = 1;
    } else {
        *(float *)(*(char **)(arg0 + 0x50) + 0xC4) = *(float *)0x70000004;
    }
}
