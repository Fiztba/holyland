void *func_0011E1F0(int, int);
int func_00211D90(void *, double);
extern int D_00446770;

typedef struct {
    char pad[0xC0];
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    Vec3 *v;
} Node;

void func_002131C0(char *arg0) {
    Node *temp_v0;

    temp_v0 = func_0011E1F0(D_00446770, *(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        func_00211D90(arg0 + 0xC, temp_v0->v->x);
        func_00211D90(arg0 + 0x14, temp_v0->v->y);
        func_00211D90(arg0 + 0x1C, temp_v0->v->z);
    }
    *(int *)arg0 = 0;
}
