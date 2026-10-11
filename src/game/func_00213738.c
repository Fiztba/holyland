extern char D_00413F80[];

typedef struct {
    char pad0[0x5C];
    int f5C;
    char pad1[0x58];
    int fB8;
} G;

void func_00213738(int *arg0) {
    G *g = (G *)D_00413F80;
    int temp_v1;

    temp_v1 = arg0[2] + g->fB8;
    if (temp_v1 < 0) {
        g->fB8 = 0;
    } else {
        if (g->f5C < temp_v1) {
            g->fB8 = g->f5C;
        } else {
            g->fB8 = temp_v1;
        }
    }
    arg0[0] = 0;
}
