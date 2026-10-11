typedef struct {
    char b[4];
} Tag4;

extern char D_00442170[];
extern int D_00437938[];

int func_0021DF68(void *, int *, int *, int *, int *);
int func_0021DF70(int *, int);
int func_0021D900(void *);
int func_0021E3C8(int, int, int, int, void *, int, int, int);

int func_0021E080(void *arg0) {
    Tag4 tmp;
    int sp10;
    int sp14;
    int sp18;
    int sp1C;
    int var_v0;

    tmp = *(Tag4 *)D_00442170;
    func_0021DF68(arg0, &sp10, &sp14, &sp18, &sp1C);
    var_v0 = sp14 + sp1C;
    if (var_v0 < 4) {
        return 0;
    }
    {
        int r = func_0021E3C8((sp10 & 0x0FFFFFFF) | 0x20000000, sp14, (sp18 & 0x0FFFFFFF) | 0x20000000, sp1C, &tmp, 4, 0, 0);
        func_0021DF70(D_00437938, r);
    }
    func_0021D900((char *)arg0 + 0x48);
    var_v0 = *(int *)((char *)arg0 + 0xA8);
    if (var_v0 == 0) {
        *(int *)((char *)arg0 + 0xA8) = 2;
    }
    var_v0 = 1;
    return var_v0;
}
