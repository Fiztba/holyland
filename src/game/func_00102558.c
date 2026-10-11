typedef struct { int active; char pad[0x28]; } Slot;
extern Slot D_00447600[];
extern int D_00446710;
void func_00102790(int, int);
void func_001024B0(int);

void func_00102558(void) {
    int i;
    Slot *s = D_00447600;
    for (i = 0; i < 2; i++, s++) {
        if (s->active) {
            func_00102790(i, 0);
            func_001024B0(i);
        }
    }
    D_00446710 = 0;
}
