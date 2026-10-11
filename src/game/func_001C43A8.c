typedef struct Obj Obj;
struct Obj {
    char pad[0x1B8];
    void *(*get)(Obj *, int);
};

typedef struct {
    Obj *p0;
    int pad[3];
    int flags;
} Owner;

extern void func_0020DF38(void *, void *, void *, int, float);

void func_001C43A8(Owner *arg0, int arg1, float arg2) {
    void *t0;
    void *t1;
    void *t2;
    void *t3;

    if (!(arg0->flags & 0x10000)) {
        return;
    }
    t0 = arg0->p0->get(arg0->p0, 0);
    t1 = arg0->p0->get(arg0->p0, 1);
    t2 = arg0->p0->get(arg0->p0, 2);
    t3 = arg0->p0->get(arg0->p0, 3);
    func_0020DF38(t0, t1, t2, arg1, arg2);
    if (*(int *)0x70000010 == 0) {
        func_0020DF38(t0, t2, t3, arg1, arg2);
    }
}
