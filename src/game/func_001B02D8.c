int sprintf(char *, const char *, ...);
extern char D_004468F0[];
extern char D_004468F8[];
extern char D_005742E0[];

void func_001B02D8(int *arg0, int arg1) {
    int i;

    if (*arg0 < 0) {
        *arg0 = arg1 - 1;
    }
    if (*arg0 >= arg1) {
        *arg0 = 0;
    }
    for (i = 0; i < arg1; i++) {
        if (i == *arg0) {
            sprintf(&D_005742E0[i * 4], D_004468F0);
        } else {
            sprintf(&D_005742E0[i * 4], D_004468F8);
        }
    }
}
