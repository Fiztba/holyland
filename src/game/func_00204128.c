extern char D_0042A2A0[];
extern int D_00446DEC;

void func_00204128(char *arg0) {
    int *ents = (int *)(arg0 + 0x10);
    int i;
    int n = *(int *)(arg0 + 4);

    D_00446DEC = n;
    for (i = 0; i < n; i++) {
        int off = ents[i * 4];
        char *dst = D_0042A2A0 + i * 0x18;
        *(char **)(dst + 0x10) = (char *)(off + (int)arg0);
    }
}
