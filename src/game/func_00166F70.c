typedef struct {
    char pad[0x82];
    short s82;
    char pad84[0x88 - 0x84];
    unsigned int v88;
} Mid;

typedef struct {
    char pad[0xFC];
    Mid *mid;
} Owner;

typedef struct {
    char pad[0x430];
    short s430;
    short s432;
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
    char pad0[0xC - 4];
    Ctl *ctl;
    char pad1[0xC6 - 0x10];
    unsigned short flags_c6;
    char pad2[0xD8 - 0xC8];
    Flags f;
} Actor;

int func_00192EB0(void *, int);
int func_00194368(float, void *);
void func_00167168(void *, int);
void func_00196120(float, void *, void *, int, void *);

void func_00166F70(Actor *arg0) {
    float v[4];
    unsigned int temp_a0;
    long temp_a1;
    char *temp_a2;
    int bit;

    temp_a1 = *(long *)&arg0->f;
    temp_a2 = (char *)arg0 + 0xE0;
    if (((int) ((long) (temp_a1 << 0x12) >> 0x20)) & 1) {
        arg0->f.b11 = 0;
        temp_a0 = arg0->owner->mid->v88;
        if (temp_a0 != 0) {
            arg0->f.b34 = func_00194368((arg0->flags_c6 & 0x12) ? *(float *)(temp_a2 + 0x40) : 1.0f / (float) temp_a0, arg0->owner);
            func_00167168(arg0, 0);
        } else {
            arg0->f.b34 = func_00192EB0(arg0, arg0->f.b11);
            if (arg0->owner->mid->s82) {
                func_00167168(arg0, 1);
            }
        }
        v[1] = arg0->ctl->s430 * 0.017453292f;
        v[0] = -arg0->ctl->s430 * 0.017453292f;
        v[3] = arg0->ctl->s432 * 0.017453292f;
        v[2] = -arg0->ctl->s432 * 0.017453292f;
        bit = ((int) (*(long *)((char *)arg0 + 0xD8) >> 9)) & 1;
        if (bit) {
            func_00196120(12.0f, arg0, temp_a2 + 0x20, bit, v);
        } else {
            func_00196120(8.0f, arg0, temp_a2 + 0x20, 0, 0);
        }
    }
}
