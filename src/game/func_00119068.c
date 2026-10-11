void func_00102228();
int func_001024B0(int);
int func_00102790(int, int);
extern int D_0038D680[];

void func_00119068(void) {
    int n;

    if (func_00102790(D_0038D680[8], 1) == 0) {
        func_001024B0(D_0038D680[8]);
        func_00102228();
        n = D_0038D680[6] + 1;
        D_0038D680[8] = -1;
        D_0038D680[6] = n;
    }
}
