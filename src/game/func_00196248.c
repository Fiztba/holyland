extern int D_00413F80[];

void func_00216778(void *, void *);
int func_0018E3D0(void *, int, int, int, void *, int, float);

int func_00196248(char *arg0, int arg1, int arg2, int arg3, float arg4) {
    float vec[2];
    char *temp;

    func_00216778(vec, D_00413F80);
    vec[1] = vec[1] - 115.0f;
    temp = *(char **)arg0;
    if (*(int *)(temp + 0x134) == 0) {
        return 0;
    }
    return func_0018E3D0(temp, *(int *)(temp + 0x134), arg1, arg2, vec, arg3, arg4);
}
