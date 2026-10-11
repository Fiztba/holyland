extern char D_00431BF0[];
extern char D_00431C30[];
void sceVu0CopyMatrix(void *, void *);
void sceVu0DivVectorXYZ(void *, void *, float);

typedef struct {
    char pad0[0xE0];
    float f0;
    float f4;
    float f8;
} Actor;

void func_0020BCB8(Actor *arg0, char *arg1, char *arg2) {
    sceVu0CopyMatrix(arg1, D_00431BF0);
    sceVu0DivVectorXYZ(arg1, arg1, arg0->f0);
    sceVu0DivVectorXYZ(arg1 + 0x10, arg1 + 0x10, arg0->f4);
    sceVu0DivVectorXYZ(arg1 + 0x20, arg1 + 0x20, arg0->f8);
    sceVu0CopyMatrix(arg2, D_00431C30);
}
