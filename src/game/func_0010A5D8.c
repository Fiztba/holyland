void func_00109A50(void);
void func_00109A80(int, unsigned long);
void func_00109AD0(void);
void func_0010A338(void *);

typedef struct {
    int pad0[4];
    int f10;
    char f14[4];
    unsigned char pad18[8];
    unsigned short f20;
    unsigned short f22;
    int f24;
    unsigned char pad28[8];
    unsigned short f30;
    unsigned short f32;
    int f34;
} Ctx;

void func_0010A5D8(void) {
    Ctx s;

    func_00109A50();
    func_00109A80(0x47, 0x30000);
    func_00109A80(0x4E, 0x10A000118UL);
    func_00109AD0();
    s.f10 = 6;
    s.f20 = 0x6C00;
    s.f22 = 0x7200;
    s.f30 = 0x9400;
    s.f32 = 0x8E00;
    s.f14[0] = 0;
    s.f14[1] = 0;
    s.f14[2] = 0;
    s.f14[3] = 0;
    s.f24 = 0;
    s.f34 = 0;
    func_0010A338(&s);
    func_00109A50();
    func_00109A80(0x47, 0x70000);
    func_00109A80(0x4E, 0x0A000118);
    func_00109AD0();
}
