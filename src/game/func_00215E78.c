void *func_001267B0(int);
int func_00211D90(void *, double);

typedef struct {
    char pad[0x10];
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    char pad[0x10];
    Vec3 *v;
} Node;

void func_00215E78(char *arg0) {
    Node *temp_v0;

    temp_v0 = func_001267B0(*(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        func_00211D90(arg0 + 0xC, temp_v0->v->x);
        func_00211D90(arg0 + 0x14, temp_v0->v->y);
        func_00211D90(arg0 + 0x1C, temp_v0->v->z);
    } else {
        func_00211D90(arg0 + 0xC, 0);
        func_00211D90(arg0 + 0x14, 0);
        func_00211D90(arg0 + 0x1C, 0);
    }
    *(int *)arg0 = 0;
}
