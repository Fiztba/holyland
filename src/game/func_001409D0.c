extern void *D_004467E0;
float func_00194F80(int, int);

typedef struct Ent {
    int *p0; int pad4[2]; char *p0C; char pad10[0xC]; struct Ent *next; char pad20[0xB8];
    long d;
} Ent;

void func_001409D0(Ent *arg0) {
    Ent *s0 = D_004467E0;
    while (s0 != 0) {
        if (arg0->p0C == s0->p0C) {
            if (*(int *)((char *)s0 + 0xD8) & 1) {
                long x = s0->d;
                if (((int)(x >> 1) & 1) && !((int)(x >> 2) & 3) && s0 != arg0) {
                    char *t = (char *)s0 + 0xE0;
                    if (t[0x7A] == 0 &&
                        func_00194F80((int)arg0->p0 + 0xC0, (int)s0->p0 + 0xC0) <
                            (float)*(short *)(arg0->p0C + 0x438)) {
                        t[0x7A] = 1;
                    }
                }
            }
        }
        s0 = s0->next;
    }
}
