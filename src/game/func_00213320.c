void *func_0011E1F0(int, int);
void func_0011E7B0(void *, int, int);
void func_0011E810(void *, int);
extern int D_00446770;

void func_00213320(char *arg0) {
    void *temp_v0;

    temp_v0 = func_0011E1F0(D_00446770, *(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        func_0011E7B0(temp_v0, *(int *)(arg0 + 0x10), *(int *)(arg0 + 0x18));
        func_0011E810(temp_v0, 0);
    }
    *(int *)arg0 = 0;
}
