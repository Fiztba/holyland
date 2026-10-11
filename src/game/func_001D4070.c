void FlushCache(int);
void func_002007C0(char *);

typedef struct {
    char pad00[8];
    int a08;
    char *p0C;
    char pad10[0xC];
    int a1C;
    int a20;
} Obj;

void func_001D4070(Obj *arg0) {
    char *temp_v1;

    temp_v1 = *(char **)(arg0->p0C + 4);
    func_002007C0(temp_v1 + *(int *)(temp_v1 + 4));
    FlushCache(0);
    arg0->a20 = 1;
    arg0->a1C = 0;
    arg0->a08 = -1;
}
