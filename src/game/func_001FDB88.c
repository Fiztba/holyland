typedef struct {
    unsigned long a : 16;
    unsigned long b : 16;
    unsigned long c : 24;
    unsigned long d : 8;
} Packed;

extern Packed D_00446D60;

int func_001FDB88(Packed arg0) {
    D_00446D60.a = arg0.a;
    D_00446D60.b = arg0.b;
    D_00446D60.c = arg0.c;
    D_00446D60.d = arg0.d;
    return 1;
}
