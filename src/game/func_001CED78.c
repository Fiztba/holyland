int func_001CBAE0();
int sceSifCallRpc(int *, int, int, char *, int, int *, int, int, int);
char *strcpy(char *, const char *);
extern char D_00420A40[];
extern int D_005BF280[];
extern int D_005BF890[];

int func_001CED78(int arg0, char *arg1, int arg2) {
    if (func_001CBAE0(1, 0x3040, arg0, arg2) != 0) {
        func_001CBAE0(1, 0x3030, arg0);
        return -1;
    }
    strcpy(D_00420A40, arg1);
    sceSifCallRpc(D_005BF890, arg0 | 0x30A0, 0, D_00420A40, 0x100, D_005BF280, 1, 0, 0);
    if (D_005BF280[0] != 0) {
        func_001CBAE0(1, 0x3030, arg0);
        return -1;
    }
    return arg0;
}
