extern long D_0043EF60[];
void func_001AF928(char *, int);
void func_001AF640(void *, int);
short func_0019AA28(float, void *, void *);

void func_001AD730(char *arg0, int arg1) {
    long tmp[2];

    tmp[0] = D_0043EF60[0];
    tmp[1] = D_0043EF60[1];
    func_001AF928(arg0, arg1);
    func_001AF640(arg0, arg1);
    *(short *)(arg0 + 0x44) = func_0019AA28(0.0f, *(char **)(arg0 + 0x50) + 0xC0, tmp);
}
