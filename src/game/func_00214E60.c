int rand(void);
int func_00211D90(void *, double);

void func_00214E60(char *arg0) {
    int n;

    n = *(float *)(arg0 + 0x8) * 100.0f;
    func_00211D90(arg0 + 0xC, (float) (rand() % n) * 0.01f);
    *(int *)arg0 = 0;
}
