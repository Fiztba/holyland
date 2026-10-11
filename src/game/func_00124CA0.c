typedef struct Ent {
    char pad0[0x18];
    struct Ent *prev;
    struct Ent *next;
} Ent;

extern Ent *D_004470B0;
extern Ent *D_004470B4;
extern int D_004470BC;

Ent *func_00124CA0(Ent *e) {
    Ent *next;

    next = e->next;
    if (D_004470B0 == e) {
        D_004470B0 = next;
    }
    if (D_004470B4 == e) {
        D_004470B4 = e->prev;
    }
    if (e->prev != 0) {
        e->prev->next = next;
    }
    if (e->next != 0) {
        e->next->prev = e->prev;
    }
    D_004470BC--;
    return next;
}
