void func_00216D20();
void func_00216CA8(float, void *, void *);
void func_00216C48(void *, void *, void *);

void func_001AFC88(void *arg0, float f1, float f2) {
    int tmp[4];
    func_00216D20(tmp);
    func_00216CA8(f2, tmp, tmp);
    func_00216C48(arg0, arg0, tmp);
    func_00216D20(arg0, arg0);
    func_00216CA8(f1, arg0, arg0);
}
