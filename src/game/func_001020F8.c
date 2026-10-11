typedef struct {
    char pad[0x24];
    unsigned int b0 : 1;
    unsigned int b1 : 1;
    unsigned int b2 : 1;
    unsigned int b3 : 1;
    unsigned int b4 : 1;
    unsigned int b5 : 1;
    unsigned int b6 : 1;
    unsigned int b7 : 1;
    unsigned int b8 : 1;
    unsigned int b9 : 5;
    unsigned int b14 : 1;
} A;

extern A D_0038CC50;

void func_001187B0(void);

void func_001020F8(void) {
    if (D_0038CC50.b2 || D_0038CC50.b3 || D_0038CC50.b4 || D_0038CC50.b5 ||
        D_0038CC50.b8 || D_0038CC50.b14) {
        D_0038CC50.b2 = 0;
        D_0038CC50.b3 = 0;
        D_0038CC50.b5 = 0;
        D_0038CC50.b4 = 0;
        D_0038CC50.b8 = 0;
        D_0038CC50.b14 = 0;
        func_001187B0();
    }
}
