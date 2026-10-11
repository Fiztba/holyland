extern int func_001024B0(int);
extern int func_00102790(int, int);
extern int D_00446AD0;

void func_001D3FF8(int *arg0) {
    int temp_v0;

    temp_v0 = func_00102790(arg0[2], 1);
    if (temp_v0 == 0) {
        func_001024B0(arg0[2]);
        D_00446AD0 = -1;
        arg0[7] = arg0[7] + 1;
    } else if (temp_v0 < 0) {
        func_001024B0(arg0[2]);
        arg0[8] = arg0[7] = 0;
        D_00446AD0 = -1;
    }
}
