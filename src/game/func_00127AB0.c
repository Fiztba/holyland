typedef struct {
    char pad[0x88];
    unsigned int v88;
} Mid;

typedef struct {
    char pad[0xFC];
    Mid *mid;
} Owner;

typedef struct {
    Owner *owner;
    char pad1[0xC6 - 4];
    unsigned short flags_c6;
    char pad2[0xD8 - 0xC8];
    unsigned long b0 : 14;
    unsigned long b14 : 1;
    unsigned long b15 : 19;
    unsigned long b34 : 1;
    unsigned long b35 : 29;
} Actor;

int func_00192EB0(void *, int, void *);
int func_00194368(float, void *);

void func_00127AB0(Actor *arg0) {
    unsigned int temp_a0;
    long temp_a1;
    int m;
    char *temp_a2;

    temp_a1 = *(long *)((char *)arg0 + 0xD8);
    temp_a2 = (char *)arg0 + 0xE0;
    if (((int) ((long) (temp_a1 << 0x12) >> 0x20)) & 1) {
        temp_a0 = arg0->owner->mid->v88;
        if (temp_a0 != 0) {
            arg0->b34 = func_00194368((arg0->flags_c6 & 0x12) ? *(float *)(temp_a2 + 0x40) : 1.0f / (float) temp_a0, arg0->owner);
        } else {
            m = ((int) (temp_a1 >> 11)) & 7;
            arg0->b34 = func_00192EB0(arg0, m, temp_a2);
        }
    }
}
