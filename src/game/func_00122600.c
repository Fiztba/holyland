typedef struct { char pad[0x178]; unsigned int flags; } Obj;

void func_00122600(Obj **arg0, int arg1) {
    Obj *p;
    unsigned int idx;
    int sel;

    p = *arg0;
    idx = arg1 - 0x86;
    sel = (idx <= 4U) ? (int)idx : -1;

    p->flags &= ~1u;
    if (sel != 0) {
        p->flags |= 1;
    }
    p->flags &= ~2u;
    if (sel != 1) {
        p->flags |= 2;
    }
    p->flags &= ~4u;
    if (sel != 2) {
        p->flags |= 4;
    }
    p->flags &= ~8u;
    if (sel != 3) {
        p->flags |= 8;
    }
    (*arg0)->flags &= ~0x10u;
    if (sel != 4) {
        (*arg0)->flags |= 0x10;
    }
}
