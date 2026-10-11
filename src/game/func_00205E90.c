typedef struct {
    unsigned long long q[12];
} Ent;

typedef struct {
    char b[0x18];
} Tbl;

typedef struct {
    char pad0[0x3F];
    signed char idx;
} Actor;

extern char D_005DD9A0[];
extern char *D_00441030[];

void *func_00205E90(Actor *arg0) {
    char *tbl[6];

    *(Tbl *)tbl = *(Tbl *)D_00441030;
    *(Ent *)D_005DD9A0 = *(Ent *)arg0;
    if (*(unsigned short *)tbl[arg0->idx] == 0) {
        return 0;
    }
    return D_005DD9A0;
}
