typedef struct {
    int f0;
    int f4;
    int f8;
    int pad_c;
    int f10;
    int pad_14;
    int f18;
    int f1C;
} Func1A1458Ctx;

int FlushCache(int);
int func_001A1360(char *, char *, int, int, int);
int func_001A1438();
int func_001CD6B8(int);
void func_001CD7E0(int);

void func_001A1458(Func1A1458Ctx *arg0) {
    while (func_001A1438() == 0);
    arg0->f10 = func_001A1360((char *)arg0->f4, (char *)arg0->f8, arg0->f0, arg0->f18, arg0->f1C);
    while (func_001A1438() == 0);
    while (func_001CD6B8(0) == 0);
    func_001CD7E0(arg0->f10);
    FlushCache(0);
}
