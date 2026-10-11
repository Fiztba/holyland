void sceVu0CopyMatrix(int, int *);
void func_00216B48(int, int *, int);
void sceVu0RotMatrixZ(float, int, int);
void sceVu0TransMatrix(int, int, int);
extern int D_00431AF0[];
extern int D_00432620[];
extern int D_004323E0[];

void func_0020C6A0(int arg0, int arg1, float *arg2, float *arg3) {
    int temp_s3;
    float t;
    float *vu;

    vu = (float *)0x70000000;
    sceVu0CopyMatrix(0x70000000, D_00431AF0);
    temp_s3 = arg0 + 0x40;
    vu[0] *= arg3[0];
    vu[5] *= arg3[1];
    t = arg2[2] + 3.1415927f;
    sceVu0RotMatrixZ(t - (float)(int)(t / 6.2831855f) * 6.2831855f - 3.1415927f, 0x70000000, 0x70000000);
    func_00216B48(0x70000000, D_00432620, 0x70000000);
    sceVu0TransMatrix(temp_s3, 0x70000000, arg1);
    func_00216B48(arg0, D_004323E0, temp_s3);
}
