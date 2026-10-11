extern void *D_004467E0;
extern void *D_004470A8;
extern void *D_004470B0;

void *func_00124C08(void *);
void *func_00124CA0(void *);
void *func_00124D48(void *);
void func_00123AB8(void *);
void func_001243E8(char);
void func_001A32A8(void *);

typedef struct Ent {
    char pad0[0x1C];
    struct Ent *next;
    char pad20[0xB3];
    char d3;
    char pad_d4;
    char d5;
} Ent;

void func_001231C0(int arg0) {
    Ent *s;

    s = D_004470A8;
    while (s != 0) {
        if (s->d5 == arg0) {
            s = func_00124C08(s);
        } else {
            s = s->next;
        }
    }

    s = D_004470B0;
    while (s != 0) {
        if (s->d5 == arg0) {
            s = func_00124CA0(s);
        } else {
            s = s->next;
        }
    }

    s = D_004467E0;
    while (s != 0) {
        if (s->d5 == arg0) {
            func_001243E8(s->d3);
            func_00123AB8(s);
            s = func_00124D48(s);
            func_001A32A8(s);
        } else {
            s = s->next;
        }
    }
}
