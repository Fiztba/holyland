void func_001D3608(int);
extern int D_00446AC8;
extern int D_0043FC28[];
extern char D_005CE0E0[];

int func_001D77F0(void) {
    int (*temp_v1)(char *);

    func_001D3608(0);
    temp_v1 = (int (*)(char *))D_0043FC28[D_00446AC8];
    if (temp_v1 != 0) {
        return temp_v1(D_005CE0E0);
    }
    return 1;
}
