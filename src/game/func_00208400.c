extern int D_00447358;
extern int D_00447364;
extern int D_00447378;
extern int D_0044737C;

void func_00208400(void) {
    int temp_v1;

    temp_v1 = D_0044737C + 1;
    D_0044737C = temp_v1;
    if (temp_v1 < D_00447378) {
        D_00447364 = (int) (temp_v1 << 7) / (int) D_00447378;
        return;
    }
    D_00447364 = 0x80;
    D_00447358 += 1;
}
