extern char D_005FAF80[];
extern int D_00446F0C;
extern int D_00446F10;
extern int D_00446F14;
extern int D_00446F18;
extern int D_00446F1C;
extern int D_00446F20;
extern int D_00446F24;
extern int D_00446F28;
extern int D_00446F2C;

void func_0020B2E8(void *, int);
void func_0020B3D0(void);

void func_0020B208(void) {
    func_0020B2E8(D_005FAF80, 0x580000);
    D_00446F0C = 0x280000;
    D_00446F10 = 0x1D0000;
    D_00446F18 = 0xA0000;
    D_00446F1C = 0x200000;
    D_00446F20 = 0xA80000;
    D_00446F14 = 0x300000;
    D_00446F24 = 0x20000;
    D_00446F28 = 0x80000;
    D_00446F2C = 0x60000;
    func_0020B3D0();
}
