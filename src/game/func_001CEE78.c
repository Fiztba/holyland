void FlushCache(int);
extern char *D_004471D4;

typedef struct {
    int id;
    int flags;
    short a;
    short b;
    int c;
    int d;
    short e;
    short f;
} Rec;

typedef struct {
    char pad[0x440];
    Rec tab[4];
} G;

int func_001CEE78(int arg0, short arg1, short arg2, short arg3, short arg4, short arg5, short arg6, int arg7) {
    Rec *r;
    int i;

    int slot;

    for (r = ((G *)D_004471D4)->tab, i = 0; i < 4; i++, r++) {
        if (r->id != -1) {
            continue;
        }
        slot = i;
        break;
    }
    if (!(i < 4)) {
        return -2;
    }
    r->id = arg7;
    r->flags = arg0 | 0x100;
    r->a = arg1;
    r->b = arg2;
    r->c = arg3;
    r->d = arg4;
    r->e = arg5;
    r->f = arg6;
    FlushCache(0);
    return slot;
}
