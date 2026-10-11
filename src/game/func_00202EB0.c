typedef struct {
    int a;
    int b;
} Arg8;

void func_002020A0(int, Arg8);

void func_00202EB0(int arg0, Arg8 arg1) {
    int var_s0;
    int temp_s1;

    var_s0 = arg0;
    temp_s1 = arg0 + 0x930;
    do {
        func_002020A0(var_s0, arg1);
        var_s0 += 0x70;
    } while (var_s0 < temp_s1);
}
