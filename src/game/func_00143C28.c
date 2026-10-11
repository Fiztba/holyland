void *func_001267B0(int);

typedef struct {
    char pad0[0xC];
    char *ctrl;
    char pad1[0xCC - 0x10];
    unsigned char state;
    char pad2[0xD8 - 0xCD];
    unsigned long long flags;
    char pad3[0x148 - 0xE0];
    unsigned short f148;
} Actor;

void func_00143C28(char *arg0) {
    Actor *temp_v0;

    temp_v0 = func_001267B0(*(unsigned char *)(*(char **)(arg0 + 0x10) + 0x41));
    if (temp_v0 != 0) {
        if ((temp_v0->flags & 0xE) == 2) {
            temp_v0->state = 2;
            temp_v0->f148 = *(unsigned short *)(temp_v0->ctrl + 0x43A);
        }
    }
}
