void func_001CBEA0(void);
int func_001CBAE0();
int FlushCache(int);
void func_001CD6E0(int);
void func_001CE408(int);
extern char *D_004471D4;

void func_001CB9A0(void) {
    char *var_s0;
    int var_s1;

    func_001CBEA0();
    func_001CBAE0(1, 0x3030, 0);
    func_001CBAE0(1, 0x3030, 1);
    var_s0 = D_004471D4 + 0x200;
    for (var_s1 = 0; var_s1 < 4; var_s1++) {
        if (*(int *)var_s0 != 0) {
            func_001CE408(var_s1);
        }
        var_s0 += 0x10;
    }
    var_s0 = D_004471D4;
    for (var_s1 = 0; var_s1 < 0x10; var_s1++) {
        if (*(int *)var_s0 != 0) {
            func_001CD6E0(var_s1);
        }
        var_s0 += 0x20;
    }
    func_001CBAE0(1, 0x20);
    FlushCache(0);
}
