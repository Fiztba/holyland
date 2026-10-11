typedef struct {
    char pad[0x1C8];
    unsigned long flags;
} State;

extern State D_00413F80;
extern void (*D_004145C8[])(unsigned short, unsigned short);
extern int D_00447128;

int func_001BD710(unsigned short arg0, unsigned short arg1) {
    State *g = &D_00413F80;

    if ((int)(g->flags >> 36) & 1) {
        D_004145C8[D_00447128](arg0, arg1);
    }
    return (int)(g->flags >> 36) & 1;
}
