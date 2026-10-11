extern int func_001CDF90(int, int, int, int);
extern void func_001CE468(int, int);
extern int D_00446858;

typedef struct {
    int *ctl;
    int pad04[3];
    int f10;
    int f14;
    int f18;
} Job;

int func_0019F090(Job *arg0) {
    int *v;
    int r;

    v = arg0->ctl;
    r = func_001CDF90(arg0->f18, arg0->f10, v[2], v[5]);
    arg0->f14 = r;
    if (r >= 0) {
        func_001CE468(r, 0);
        D_00446858 += 1;
        return 0;
    }
    return -1;
}
