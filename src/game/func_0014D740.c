extern float D_00413F84[];
extern float D_004452F0;
extern float D_004452F4;

double atan2(double, double);
float func_00194FB8(float);
float func_00195388(float *, float, float);

typedef struct {
    char pad0[0x48];
    float f48;
} Sub;

typedef struct {
    char pad0[0x50];
    float f50;
} Ctl;

typedef struct {
    char pad0[0xC4];
    float fC4;
    char padC8[0x30];
    char *pF8;
} Obj;

void func_0014D740(Obj **arg0) {
    Obj *o = *arg0;
    Ctl *c = (Ctl *)(o->pF8 + 0x580);
    Sub *s = (Sub *)((char *)arg0 + 0xE0);
    float a;

    if (c != 0) {
        a = func_00194FB8(atan2(D_00413F84[0] - o->fC4, *(float *)((char *)arg0 + 0x64)));
        if (D_004452F0 < a) {
            a = D_004452F0;
        } else if (a < D_004452F4) {
            a = D_004452F4;
        }
        func_00195388(&s->f48, a, 10.0f);
        c->f50 = s->f48;
    }
}
