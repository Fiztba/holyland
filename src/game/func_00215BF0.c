void *func_001267B0(int);
int func_00211D90(void *, double);
double fptodp(float);

void func_00215BF0(char *arg0) {
    char *temp_v0;

    temp_v0 = func_001267B0(*(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        func_00211D90(arg0 + 0xC, fptodp(*(float *)(*(char **)temp_v0 + 0xC0)));
        func_00211D90(arg0 + 0x14, fptodp(*(float *)(*(char **)temp_v0 + 0xC4)));
        func_00211D90(arg0 + 0x1C, fptodp(*(float *)(*(char **)temp_v0 + 0xC8)));
    } else {
        func_00211D90(arg0 + 0xC, 0);
        func_00211D90(arg0 + 0x14, 0);
        func_00211D90(arg0 + 0x1C, 0);
    }
    *(int *)arg0 = 0;
}
