extern char D_0038CC53[];
extern float D_004325A0[];

typedef struct {
    int a00;
    int a04;
    float f08;
    int a0c;
    float f10;
    int a14;
    float f18;
    int a1c;
    int a20;
} Obj;

void func_00213AB8(Obj *arg0) {
    if (D_0038CC53[0] != 0 && (arg0->f08 != D_004325A0[0] || arg0->f10 != D_004325A0[1] || arg0->f18 != D_004325A0[2])) {
        int n = arg0->a20;
        float d;

        if (n == 0) {
            arg0->a20 = 1;
            n = 1;
        }
        d = (float)n;
        n--;
        arg0->a20 = n;
        D_004325A0[0] = D_004325A0[0] + (arg0->f08 - D_004325A0[0]) / d;
        D_004325A0[1] = D_004325A0[1] + (arg0->f10 - D_004325A0[1]) / d;
        D_004325A0[2] = D_004325A0[2] + (arg0->f18 - D_004325A0[2]) / d;
        if (n != 0) {
            return;
        }
    }
    arg0->a00 = 0;
}
