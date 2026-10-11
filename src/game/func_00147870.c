typedef struct {
    char pad[0x88];
    unsigned int v88;
} Mid;

typedef struct {
    char pad[0xFC];
    Mid *mid;
} Owner;

typedef struct {
    char pad[0x430];
    short x430;
    short x432;
} Ctl;

typedef struct {
    unsigned long b0 : 11;
    unsigned long b11 : 3;
    unsigned long b14 : 1;
    unsigned long b15 : 19;
    unsigned long b34 : 1;
    unsigned long b35 : 29;
} Flags;


typedef struct {
    Owner *owner;
    char pad0[8];
    Ctl *ctl;
    char pad1[0xC6 - 0x10];
    unsigned short flags_c6;
    char pad2[0xD8 - 0xC8];
    Flags f;
} Actor;

void func_00147A20(void *, int);
int func_00192EB0(void *, int);
int func_00194368(float, void *);
void func_00196120(float, void *, void *, int, float *);

void func_00147870(Actor *arg0) {
    float v[4];
    unsigned int temp_a0;
    long temp_a1, t2;
    char *temp_a2;
    unsigned c;

    temp_a1 = *(long *)&arg0->f;
    temp_a2 = (char *)arg0 + 0xE0;
    if (((int) ((long) (temp_a1 << 0x12) >> 0x20)) & 1) {
        arg0->f.b11 = 0;
        temp_a0 = arg0->owner->mid->v88;
        if (temp_a0 != 0) {
            arg0->f.b34 = func_00194368((arg0->flags_c6 & 0x12) ? *(float *)(temp_a2 + 0x40) : 1.0f / (float) temp_a0, arg0->owner);
        } else {
            arg0->f.b34 = func_00192EB0(arg0, arg0->f.b11);
        }
        func_00147A20(arg0, temp_a2[0x7A]);
        t2 = *(long *)((char *)arg0 + 0xD8);
        c = ((int) (t2 >> 9)) & 1;
        v[1] = (float) arg0->ctl->x430 * 0.017453292f;
        v[0] = (float) -arg0->ctl->x430 * 0.017453292f;
        v[3] = (float) arg0->ctl->x432 * 0.017453292f;
        v[2] = (float) -arg0->ctl->x432 * 0.017453292f;
        if (c) {
            func_00196120(12.0f, arg0, temp_a2 + 0x20, c, v);
        } else {
            func_00196120(8.0f, arg0, temp_a2 + 0x20, 0, 0);
        }
    }
}
