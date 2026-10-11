typedef struct {
    char b[8];
} Stamp;

int func_001D2100(void);
int func_001D2550(Stamp);
extern Stamp D_00446A18;
extern char D_00446A29;
extern char D_00446A2A;
extern char D_00446A2B;
extern char D_00446A2D;

int func_001D22B8(void) {
    int temp_s0;
    int temp_s0_2;
    int temp_lo;
    int temp_lo_2;

    temp_s0 = func_001D2100();
    temp_s0_2 = temp_s0 + func_001D2550(D_00446A18);
    D_00446A29 = (char) (temp_s0_2 % 60);
    temp_lo = temp_s0_2 / 60;
    D_00446A2A = (char) (temp_lo % 60);
    temp_lo_2 = temp_lo / 60;
    D_00446A2B = (char) (temp_lo_2 % 99);
    D_00446A2D = (char) (temp_lo_2 / 99);
    return 1;
}
