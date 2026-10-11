void sceVu0RotMatrixX(float, int *, int *);
void sceVu0RotMatrixY(float, int *, int *);
void sceVu0ApplyMatrix(float *, int *, float *);
extern int D_00431AF0[];

void func_001AF538(float *arg0, float *arg1, float arg2) {
    int m[16];

    arg0[0] = arg0[1] = arg0[3] = 0.0f;
    arg0[2] = arg2;
    sceVu0RotMatrixX(arg1[0], m, D_00431AF0);
    sceVu0RotMatrixY(arg1[1], m, m);
    sceVu0ApplyMatrix(arg0, m, arg0);
}
