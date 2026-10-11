extern char *D_00447334;
int strcmp(char *, char *);

int func_001FB710(char *arg0, int arg1) {
    char *p;
    int i;
    int found;
    int res;

    if (D_00447334 == 0 || arg0 == 0) {
        return -1;
    }
    p = D_00447334;
    res = 0;
    found = 0;
    for (i = 0; i < 11; i++) {
        if (strcmp(p + 0x20, arg0) == 0) {
            found = 1;
            if (arg1 > 0) {
                res = (*(int *)(p + 0x10) == arg1) ? 0 : -1;
            }
            break;
        }
        p += 0x40;
    }
    return found ? res : -1;
}
