extern int D_00447334;
extern int D_0044733C;
extern char D_00440B58[];
extern char D_00440960[];
extern char D_00446D28[];
int func_001FB710(char *, int);
int sprintf(char *, char *, char *, char *, int);

int func_001FB7D0(void) {
    char buf[0x40];
    int i;
    int off;
    int ret;
    char c;

    if (D_00447334 == 0) {
        return -1;
    }
    if (D_0044733C == 0) {
        return -1;
    }
    ret = 0;
    i = 0;
    do {
        off = i * 0x180;
        c = *(char *)(D_0044733C + off);
        if (c == 1) {
            sprintf(buf, D_00440B58, D_00440960, D_00446D28, i);
            if (func_001FB710(buf, 0) == -1) {
                ret = -1;
                *(char *)(D_0044733C + off + 1) = c;
            }
        }
        i++;
    } while (i < 3);
    return ret;
}
