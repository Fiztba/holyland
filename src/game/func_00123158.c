typedef struct { char data[0x180]; } Obj;
extern Obj D_003BEDF0[][0x80];
extern int D_004467D8[2];
void func_00124BC8(Obj *);

void func_00123158(int idx) {
    int i;
    Obj *p = D_003BEDF0[idx];
    int n = D_004467D8[idx];
    for (i = 0; i < n; i++) {
        func_00124BC8(p);
        p++;
    }
}
