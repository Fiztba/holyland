typedef struct {
    unsigned char n;
    char pad0[0xB];
    unsigned short *p;
} Ent;

typedef struct {
    char pad0[0x6C];
    unsigned char tag;
    char pad1[0xB];
    unsigned long long lo : 39;
    unsigned long long f : 7;
} Rec;

typedef struct {
    char pad0[0xF0];
    unsigned short count;
    char pad1[6];
    Rec *recs;
    Ent **ents;
} Ctx;

int func_001914E8(Ctx *arg0, int arg1, int arg2, int arg3) {
    Ent **tbl = &arg0->ents[arg2];
    Ent *e;
    int i;

    for (e = *tbl, i = 0; i < arg0->count; i++, e++) {
        Rec *r = &arg0->recs[i];
        int v;
        int n;
        unsigned short *p;

        if (r->tag == arg1 && e->p != 0 && e->n != 0) {
            p = e->p;
            n = e->n;
            for (v = 0; v < n; v++) {
                if (arg3 < *p)
                    break;
                p += 9;
            }
            if (v == n) {
                r->f = 0;
            } else if (v != 0) {
                r->f = v - 1;
            }
        }
    }
    return 1;
}
