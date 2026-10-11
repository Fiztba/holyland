extern int D_00446770;

void *func_0011E1F0(int, int);

typedef struct {
    char pad[0x150];
    float a;
    float b;
    float c;
    float d;
} Target;

typedef struct {
    Target *v;
} Node;

void func_00212E18(char *arg0) {
    Node *temp_v0;

    temp_v0 = func_0011E1F0(D_00446770, *(int *)(arg0 + 0x8));
    if (temp_v0 != 0 && temp_v0->v != 0) {
        temp_v0->v->a = (float)*(int *)(arg0 + 0x10);
        temp_v0->v->b = (float)*(int *)(arg0 + 0x18);
        temp_v0->v->c = (float)*(int *)(arg0 + 0x20);
        temp_v0->v->d = (float)*(int *)(arg0 + 0x28);
    }
    *(int *)arg0 = 0;
}
