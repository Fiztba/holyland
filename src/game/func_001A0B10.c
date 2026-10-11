extern int func_001CD020(int, int, int, int, int, int, int);
extern int D_00446864;

typedef struct {
    int *ctl;
    int pad04[3];
    int f10;
    int f14;
    int f18;
    int f1C;
} Job;

int func_001A0B10(Job *arg0) {
    int *v = arg0->ctl;
    int r = func_001CD020(arg0->f18, v[1], v[4], v[0], v[3], 0, arg0->f1C);

    arg0->f10 = r;
    if (r >= 0) {
        D_00446864 += 1;
    }
    return 0;
}
