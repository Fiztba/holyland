void func_001AF928(char *, int);
void func_001AF640(void *, int);
void func_00216778(void *, void *);

void func_001ACE08(char *arg0, int arg1) {
    char *s2;

    s2 = arg0 + 0x10;
    func_001AF928(arg0, arg1);
    func_001AF640(arg0, arg1);
    *(int *)(s2 + 0x10) = *(int *)(arg0 + 0x5C);
    func_00216778(*(char **)(arg0 + 0x50) + 0xC0, *(void **)(arg0 + 0x64));
    func_00216778(*(char **)(arg0 + 0x50) + 0xD0, *(void **)(arg0 + 0x68));
    *(char *)(s2 + 0x14) = 0;
}
