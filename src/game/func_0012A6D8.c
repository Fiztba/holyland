int func_00192C98(void *, int, int, unsigned int, int);

typedef struct {
    char pad0[0xC4];
    unsigned char c4;
} Ctl;

typedef struct {
    char pad0[0xC];
    Ctl *c;
    char pad1[0xC8 - 0x10];
    unsigned short hc8;
} Actor;

void func_0012A6D8(Actor *arg0) {
    unsigned short temp_v1;

    temp_v1 = arg0->hc8;
    if (temp_v1 & 0x100) {
        if (temp_v1 & 0x400) {
            func_00192C98(arg0, 7, 0, arg0->c->c4 >> 1, 0);
            return;
        }
        func_00192C98(arg0, 0xC, 0, arg0->c->c4, 0);
        return;
    }
    if (temp_v1 & 0x200) {
        if (temp_v1 & 0x400) {
            func_00192C98(arg0, 6, 0, arg0->c->c4 >> 1, 0);
            return;
        }
        func_00192C98(arg0, 0xB, 0, arg0->c->c4, 0);
        return;
    }
    func_00192C98(arg0, 0, 0, arg0->c->c4, 0);
}
