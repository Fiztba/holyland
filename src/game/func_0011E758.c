typedef struct {
    char pad0[0x10];
    unsigned int b0 : 8;
    unsigned int b8 : 8;
    unsigned int b16 : 4;
    unsigned int f20 : 1;
    unsigned int f21 : 1;
    unsigned int f22 : 1;
    unsigned int b23 : 1;
    unsigned int b24 : 8;
} Rec;

void func_0011E758(Rec *arg0, int arg1) {
    if (arg0->f20 != arg1) {
        arg0->f20 = arg1;
        arg0->f21 = 0;
        arg0->f22 = 0;
        arg0->b24 = 0;
        arg0->b8 = 0;
    }
}
