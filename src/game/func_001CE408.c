int FlushCache(int);
int sceSifFreeIopHeap(int);
extern char *D_004471D4;

int func_001CE408(int i) {
    int *p = (int *)(D_004471D4 + (i * 16 + 0x200));
    if (p[0] == 0) {
        return -1;
    }
    if (sceSifFreeIopHeap(p[0]) != 0) {
        return -1;
    }
    p[1] = p[0] = 0;
    FlushCache(0);
    return 0;
}
