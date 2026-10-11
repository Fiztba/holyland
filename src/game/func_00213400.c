extern char D_0038CC50[];
extern float D_00413F80[];

typedef struct {
    int i0;
    int pad4;
    float f8;
    int padc;
    float f10;
    int pad14;
    float f18;
    int pad1c;
    int i20;
} Actor;

void func_00213400(Actor *arg0) {
    if (D_0038CC50[0] != 0 || (arg0->f8 == D_00413F80[0] && arg0->f10 == D_00413F80[1] && arg0->f18 == D_00413F80[2])) {
        arg0->i0 = 0;
    } else {
        int n = arg0->i20;
        float d;
        int m;

        if (n == 0) {
            arg0->i20 = 1;
            n = 1;
        }
        d = (float) n;
        m = n - 1;
        arg0->i20 = m;
        D_00413F80[0] = D_00413F80[0] + (arg0->f8 - D_00413F80[0]) / d;
        D_00413F80[1] = D_00413F80[1] + (arg0->f10 - D_00413F80[1]) / d;
        D_00413F80[2] = D_00413F80[2] + (arg0->f18 - D_00413F80[2]) / d;
        if (m == 0) {
            arg0->i0 = 0;
        }
    }
}
