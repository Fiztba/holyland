extern float D_00447058;
extern float D_00471EE0[];
extern const float D_00444BDC;
extern const float D_00444BE0;
extern const float D_00444BE4;

double sin(double);

void func_001112B0(void) {
    float x;
    int i;

    D_00447058 += D_00444BDC;
    if (D_00447058 > D_00444BE0) {
        D_00447058 -= D_00444BE0;
    }
    x = D_00447058;
    for (i = 0; i < 32; i++) {
        D_00471EE0[i] = sin(x);
        x += D_00444BE4;
        if (x > D_00444BE0) {
            x -= D_00444BE0;
        }
    }
}
