extern char D_00413F80[];

void func_001B4B10(short, int);
int func_001BBFC8(void);

int func_001B3E78(char *arg0, unsigned char *arg1) {
    if (*(char **)(arg0 + 0x48) == D_00413F80) {
        if (*arg1 == 0) {
            func_001B4B10(*(short *)(arg1 + 0x24), 4);
            if (*(int *)(arg1 + 4) == 0) {
                return func_001BBFC8();
            }
        }
    }
}
