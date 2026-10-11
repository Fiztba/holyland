void *func_001C8AF0(int);

typedef struct {
    char pad[0xC0];
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

void func_002126F8(Obj *arg0) {
    Owner *o;
    int m;

    o = (Owner *)func_001C8AF0((int)arg0->f08);
    if (o == 0) {
        arg0->f00 = 0;
        return;
    }
    if (arg0->f28 <= 0) {
        arg0->f00 = 0;
        return;
    }
    m = arg0->f28 - 1;
    arg0->f28 = m;
    o->ctl->c0 = o->ctl->c0 + arg0->f10;
    o->ctl->c4 = o->ctl->c4 + arg0->f18;
    o->ctl->c8 = o->ctl->c8 + arg0->f20;
    if (m == 0) {
        arg0->f00 = 0;
    }
}
