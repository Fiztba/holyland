extern void *D_004467E0;
extern float D_00413F80[];

typedef struct V {
    char pad0[4];
    float f4;
    char pad8[0x40];
    unsigned short u48;
    char pad4A[0x10];
    unsigned short u5A;
} V;

typedef struct Ent {
    char pad0[0x10];
    V *p10;
    char pad14[8];
    struct Ent *next;
    char pad20[0x44];
    float f64;
    char pad68[0x6B];
    char cD3;
} Ent;

float func_00124DE0(void *);
void func_001243E8(char);
void func_00123AB8(void *);
void func_00124D48(void *);
void func_00124BC8(void *);
void func_001A32A8(void *);

void func_00123580(void) {
    Ent *s = D_004467E0;
    while (s != 0) {
        V *v;
        float f;
        float d;
        int hit;
        Ent *cur;

        f = func_00124DE0(s);
        s->f64 = f;
        v = s->p10;
        d = D_00413F80[1] - v->f4;
        if (!(0.0f <= d)) {
            d = -d;
        }
        hit = 0;
        if ((float)v->u5A + 300.0f < f || (v->u48 != 0 && (float)v->u48 + 150.0f < d)) {
            hit = 1;
        }
        cur = s;
        if (hit != 0) {
            s = s->next;
            func_001243E8(cur->cD3);
            func_00123AB8(cur);
            func_00124D48(cur);
            func_00124BC8(cur);
            func_001A32A8(cur);
        } else {
            s = s->next;
        }
    }
}
