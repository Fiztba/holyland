typedef char *va_list;

extern char D_004471E0;
extern int D_005C10C8[];

int func_001D07C8(int);
int func_001CFDB8(int, int *, va_list);
int func_001D0658(int *, int);

typedef struct {
    int *p0;
    int *p4;
    int v8;
    int vC;
    int v10;
    int v14;
    int v18;
    int v1C;
    int v20;
    int v24;
    int v28;
    int v2C;
    int v30;
    int v34;
} Ctx;

extern Ctx D_005C58E8;

void func_001CF988(int a0, int a1, int a2, int a3, int a4, int a5, ...) {
    va_list ap;
    int r;

    r = func_001D07C8(a5);
    ap = (char *)__builtin_next_arg(a5) - 0x10;
    if (D_004471E0 == 0) {
        D_004471E0 = 1;
        func_001CFDB8(r, D_005C10C8, ap);
        D_005C58E8.v14 = 0;
        D_005C58E8.vC = a4;
        D_005C58E8.v10 = a4;
        D_005C58E8.p0 = D_005C10C8;
        D_005C58E8.v28 = a0 + 0x38;
        D_005C58E8.v2C = a1 + 0xF0;
        D_005C58E8.v24 = 8;
        D_005C58E8.v34 = a3;
        D_005C58E8.v18 = 0;
        D_005C58E8.v1C = 0;
        D_005C58E8.p4 = D_005C10C8;
        D_005C58E8.v30 = a2;
        D_005C58E8.v20 = 0;
        D_005C58E8.v8 = func_001D0658(D_005C10C8, a2);
    }
}
