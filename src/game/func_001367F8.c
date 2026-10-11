typedef struct {
    char pad0[0xC6];
    unsigned short flags;
    char padC8[4];
    char state;
    char padCD[10];
    unsigned char key;
} Obj;

extern unsigned int D_00446ED4;
void func_00196870(void *, int);
void func_001958B8(void *, void (*)(void), int);
void func_001368B8(void);

void func_001367F8(Obj *arg0, int arg1) {
    if (D_00446ED4 % 10 == arg0->key % 10) {
        func_00196870(arg0, 2);
    }
    if (arg0->flags == 0) {
        if (arg0->state == 3) {
            func_001958B8(arg0, func_001368B8, arg1);
        } else {
            func_001958B8(arg0, func_001368B8, arg1);
        }
    }
}
