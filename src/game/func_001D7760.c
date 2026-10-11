typedef struct {
    char pad0[0x3E];
    unsigned char unk3E;
    char pad3F[3];
    unsigned char unk42;
    char pad43[0x1D];
} Entry60;

extern unsigned int D_00446A74;
extern Entry60 *D_00446BFC;
int func_001E5210(unsigned char);
int func_00203F28(Entry60 *, int, int);

void func_001D7760(void) {
    unsigned int i;

    for (i = 0; i < D_00446A74; i++) {
        if (D_00446BFC[i].unk3E == 2) {
            func_001E5210(D_00446BFC[i].unk42);
            func_00203F28(&D_00446BFC[i], 2, 0);
            func_001E5210(0);
        }
    }
}
