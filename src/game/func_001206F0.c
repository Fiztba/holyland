int func_0011E340(void *, int);
int func_0011E3A0(void *);

void func_001206F0(void *arg0) {
    char *temp;

    temp = *(char **)(*(char **)arg0 + 0xFC);
    if (*(unsigned char *)(temp + 0x81) == 0) {
        if (*(short *)(temp + 0x82) != 0) {
            func_0011E3A0(arg0);
        } else {
            func_0011E340(arg0, 5);
        }
    } else {
        if (*(short *)(temp + 0x82) == 0) {
            func_0011E340(arg0, 4);
            *(char *)((char *)arg0 + 0x11) = 1;
        } else {
            func_0011E3A0(arg0);
        }
    }
}
