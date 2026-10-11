extern int D_00446770;
void *func_00116160(int, int);

void func_00213D88(char *arg0) {
    char *temp_v0;

    temp_v0 = func_00116160(D_00446770, *(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        if (*(int *)(arg0 + 0x10) != 0) {
            *(int *)(temp_v0 + 0x3C) = *(int *)(temp_v0 + 0x3C) & 0xBFFFFFFF;
        } else {
            *(int *)(temp_v0 + 0x3C) = *(int *)(temp_v0 + 0x3C) | 0x40000000;
        }
    }
    *(int *)arg0 = 0;
}
