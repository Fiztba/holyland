extern unsigned char D_00367980[6][5];
extern unsigned short D_00367940[6][5];

int atoi(const char *);
char *strchr(const char *, int);

void func_00207FD0(int arg0, char *arg1) {
    int idx;
    char *p;

    if ((unsigned char)(arg1[-2] - 0x31) < 5) {
        idx = arg1[-2] - 0x31;
        atoi(arg1);
        D_00367980[arg0][idx] = atoi(arg1);
        p = strchr(arg1, 0x2C);
        if (p != 0) {
            D_00367940[arg0][idx] = atoi(p + 1);
        }
    }
}
