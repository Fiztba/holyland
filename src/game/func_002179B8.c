extern char D_01E0DB00[];

int func_002179B8(int arg0, int arg1) {
    char *temp_a0;

    temp_a0 = D_01E0DB00 + ((arg0 * 4 + arg1) * 0x1C0);
    if (!(temp_a0[0x17D] & 4)) {
        return 0;
    }
    temp_a0[0x17F] = 2;
    return 1;
}
