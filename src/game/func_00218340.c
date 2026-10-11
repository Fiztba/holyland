extern int D_01E4F310[];

void func_00218340(int arg0) {
    if (arg0 >= 0) {
        D_01E4F310[0] = 0;
        return;
    }
    if (arg0 == -2) {
        D_01E4F310[0] = 3;
        return;
    }
    if (arg0 == -3) {
        D_01E4F310[0] = 0x1E;
        return;
    }
    if (arg0 == -4) {
        D_01E4F310[0] = 0x64;
        return;
    }
    if (arg0 == -5) {
        D_01E4F310[0] = 0x64;
        return;
    }
    if (arg0 == -7) {
        D_01E4F310[0] = 0x64;
        return;
    }
    if (arg0 < -0xA) {
        D_01E4F310[0] = 1;
        return;
    }
    D_01E4F310[0] = 0x64;
}
