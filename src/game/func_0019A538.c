typedef struct { int p0; char pad[8]; unsigned char *tbl; } Obj;
typedef struct { unsigned char idx; unsigned char cnt; short s; } Ent;

int func_00192100(int, int, unsigned char, short);

int func_0019A538(Obj *arg0, Ent *arg1, int arg2) {
    unsigned char *p = arg0->tbl + (arg1->idx & 0x1F);
    unsigned char v;

    func_00192100(arg0->p0, arg2, p[0x60], arg1->s);
    arg1->cnt = v = arg1->cnt - 1;
    return v;
}
