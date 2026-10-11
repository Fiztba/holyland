void *func_001C8AF0(int);
int func_0019B9F0(float, float *, int);

typedef struct {
    char pad[0xD0];
    float c0;
    float c4;
    float c8;
} Ctl;

typedef struct {
    char pad[0x18];
    Ctl *ctl;
} Owner;

typedef struct {
    int f00;
    int f04;
    void *f08;
    char pad0C[4];
    float f10;
    float f14;
    float f18;
    float f1C;
    float f20;
    char pad24[4];
    int f28;
} Obj;

void func_002125B8(Obj *arg0) {
    Owner *o;

    o = (Owner *)func_001C8AF0((int)arg0->f08);
    if (o == 0) {
        arg0->f00 = 0;
        return;
    }
    if (arg0->f10 == o->ctl->c0 && arg0->f18 == o->ctl->c4 && arg0->f20 == o->ctl->c8) {
        arg0->f00 = 0;
        return;
    }
    func_0019B9F0(arg0->f10, &o->ctl->c0, arg0->f28);
    func_0019B9F0(arg0->f18, &o->ctl->c4, arg0->f28);
    func_0019B9F0(arg0->f20, &o->ctl->c8, arg0->f28);
    if (--arg0->f28 == 0) {
        arg0->f00 = 0;
    }
}
