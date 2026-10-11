extern void *D_004469E0;

void *func_001CB530(char *arg0) {
    void *next;
    void *prev;

    next = *(void **)(arg0 + 0x1C);
    prev = *(void **)(arg0 + 0x18);
    *(int *)arg0 &= ~1;
    if (prev != 0) {
        *(void **)((char *)prev + 0x1C) = next;
    }
    if (*(void **)(arg0 + 0x1C) != 0) {
        *(void **)(*(char **)(arg0 + 0x1C) + 0x18) = *(void **)(arg0 + 0x18);
    }
    if (D_004469E0 == arg0) {
        D_004469E0 = *(void **)(arg0 + 0x1C);
    }
    return next;
}
