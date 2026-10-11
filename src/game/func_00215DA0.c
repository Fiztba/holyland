void *func_001267B0(int);
int func_00211D90(void *, double);
double fptodp(float);

typedef struct {
    char pad[0x10];
    float *f;
} Tmp;

void func_00215DA0(char *arg0) {
    Tmp *temp_v0;

    temp_v0 = func_001267B0(*(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        func_00211D90(arg0 + 0xC, fptodp(temp_v0->f[0]));
        func_00211D90(arg0 + 0x14, fptodp(temp_v0->f[1]));
        func_00211D90(arg0 + 0x1C, fptodp(temp_v0->f[2]));
    } else {
        func_00211D90(arg0 + 0xC, 0);
        func_00211D90(arg0 + 0x14, 0);
        func_00211D90(arg0 + 0x1C, 0);
    }
    *(int *)arg0 = 0;
}
