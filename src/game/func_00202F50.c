typedef struct {
    int a;
    int b;
} Arg8;

typedef struct {
    char pad[0x70];
} Elem;

void func_002020A0(Elem *, Arg8);

void func_00202F50(Elem *arg0, Arg8 arg1) {
    int i;

    for (i = 0; i < 15; i++) {
        func_002020A0(&arg0[i], arg1);
    }
    for (i = 16; i < 21; i++) {
        func_002020A0(&arg0[i], arg1);
    }
}
