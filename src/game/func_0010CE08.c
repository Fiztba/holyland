typedef struct { char pad[0x78]; unsigned long long w; } Ent;
typedef struct {
    char p0[0xF0];
    unsigned short cnt;
    char p1[6];
    Ent *ents;
    char p2[0xA8];
    long long *out;
} Obj;

long long *func_0010CE08(Obj *o, long long *a) {
    long long *s;
    int i;
    unsigned short total;
    Ent *e;
    int m;

    o->out = a;
    e = o->ents;
    s = a;
    total = o->cnt;
    for (i = 0; i < total; i++, e++) {
        int j = 0;
        int n = (int)(e->w >> 53);
        long long *p = *(long long **)&e->w;
        for (; j < n; j++) {
            long long *cur = p;
            unsigned short u = *cur;
            int k = 0;
            long long *d;
            p += 6;
            *s++ = *p;
            m = *(int *)(cur + 0x12) & 0x7FFF;
            d = cur + 0x16;
            p += 2;
            *s++ = *p;
            p += 2;
            *s++ = *p;
            p += 2;
            *s++ = *p;
            for (; k < m; k++) {
                *s++ = d[4];
                d += 8;
            }
            p = cur + (u + 1) * 2;
        }
    }
    return s;
}
