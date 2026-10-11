extern int D_004473C8;
extern int D_004473CC;
extern int D_005FAD60[];
extern int D_0042C1E0[];
extern int D_005FAD80[];
extern int D_0042C208[];

void func_00209A28(int, int, int, int, int, void *, void *);

void func_00209E78(void) {
    D_004473C8 = 0x12C;
    func_00209A28(0, -0x100, -0x20, 0x200, 0x40, D_005FAD60, D_0042C1E0);
    func_00209A28(0xC, -0x100, -0x80, 0x200, 0x100, D_005FAD80, D_0042C208);
    D_004473CC = 0;
}
