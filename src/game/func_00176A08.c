void func_00190A30(char *);
void func_00182240(void);
void func_00181378(void);
void func_001814B0(void);
void func_001816A0(void);
void func_001958B8(char *, void (*)(void), int);
void func_0019A310(char *, void (*)(void), int);

void func_00176A08(char *arg0) {
    unsigned long long f;

    func_00190A30(arg0);
    f = *(unsigned long long *)(arg0 + 0xD8);
    if ((int)(f >> 38) & 3) {
        *(unsigned long long *)(arg0 + 0xD8) = (f & ~(3ULL << 58)) | (1ULL << 57);
        func_001958B8(arg0, func_00182240, 1);
        return;
    }
    if (((int)(f >> 36) & 3) == 1) {
        if ((int)((long long)(f << 13) >> 32) & 1) {
            func_0019A310(arg0, func_00181378, 0);
            return;
        }
        func_0019A310(arg0, func_001814B0, 0);
        return;
    }
    func_0019A310(arg0, func_001816A0, 0);
}
