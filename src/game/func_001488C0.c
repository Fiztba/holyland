float func_00194FB8(float);
int func_00198730(void *, float, int);
int func_001995A0(void *, float, int);

int func_001488C0(char *arg0) {
    float t = func_00194FB8(*(float *)(*(char **)arg0 + 0xD4) + *(float *)(arg0 + 0x128));

    if (func_00198730(arg0, t, 0)) {
        return 1;
    }
    if (func_00198730(arg0, *(float *)(*(char **)arg0 + 0xD4), 1)) {
        return 1;
    }
    if (func_00198730(arg0, t, 2)) {
        return 1;
    }
    if (func_001995A0(arg0, t, 0)) {
        return 1;
    }
    if (func_001995A0(arg0, t, 1)) {
        return 1;
    }
    return func_001995A0(arg0, t, 2) != 0;
}
