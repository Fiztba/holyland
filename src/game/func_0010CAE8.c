typedef struct E {
    char pad[0x78];
    unsigned long addr : 32;
    unsigned long pad2 : 21;
    unsigned long count : 11;
} E;

typedef struct O {
    char pad[0xF0];
    unsigned short n;
    char pad2[6];
    E *e;
} O;

void func_0010CAE8(O *arg0, long arg1, long arg2, long arg3, long arg4) {
    E *e;
    char *a;
    char *base;
    int i;
    int j;
    int c;
    int n;
    unsigned short cnt;

    n = arg0->n;
    e = arg0->e;
    for (i = 0; i < n; i++, e++) {
        c = e->count;
        a = (char *)(int)e->addr;
        for (j = 0; j < c; j++) {
            base = a;
            cnt = *(unsigned short *)base;
            a += 0x30;
            if (arg1 != -1) {
                *(long *)a = arg1;
            }
            a += 0x10;
            if (arg2 != -1) {
                *(long *)a = arg2;
            }
            a += 0x10;
            if (arg3 != -1) {
                *(long *)a = arg3;
            }
            a += 0x10;
            if (arg4 != -1) {
                *(long *)a = arg4;
            }
            a = base + (cnt + 1) * 0x10;
        }
    }
}
