typedef struct {
    char pad[0x7C];
    char *act;
} Actor;

void *func_00116160(int, int);
int func_00211D90(void *, double);
extern int D_00446770;

void func_002141E0(char *arg0) {
    Actor *temp_v0;

    temp_v0 = func_00116160(D_00446770, *(int *)(arg0 + 0x8));
    if (temp_v0 != 0) {
        func_00211D90(arg0 + 0xC, *(float *)(temp_v0->act + 0xD0));
        func_00211D90(arg0 + 0x14, *(float *)(temp_v0->act + 0xD4));
        func_00211D90(arg0 + 0x1C, *(float *)(temp_v0->act + 0xD8));
    }
    *(int *)arg0 = 0;
}
