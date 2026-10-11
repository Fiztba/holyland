float func_001954A8(int, int *);
float func_001957B8(float, float);
float func_00194F80(int, int *);

int func_0015C958(char *arg0, float arg1) {
    float a;
    float f2;
    float t;
    int var;

    var = 0;
    if (*(int **)(arg0 + 0x5C) == 0) {
        return 0;
    }
    t = func_001954A8(*(int *)arg0 + 0xC0, *(int **)(arg0 + 0x5C));
    a = func_001957B8(*(float *)(*(char **)arg0 + 0xD4), t);
    f2 = *(float *)(*(char **)(arg0 + 0xC) + 0x5C);
    if (0.0f <= a ? f2 < a : f2 < -a) {
        var = 0x100;
        if (0.0f <= a) {
            var = 0x200;
        }
    }
    if (arg1 < func_00194F80(*(int *)arg0 + 0xC0, *(int **)(arg0 + 0x5C))) {
        var |= 2;
    }
    return var;
}
