typedef struct {
    unsigned char a, b, c, d;
} Slot;

typedef struct {
    char pad0[0x102];
    unsigned short f102;
    char pad1[0x1C];
    unsigned short f120;
    unsigned short f122;
    Slot slot[16];
    unsigned short f164;
} Ctl;

void func_00216EA0(void *arg0) {
    Ctl *c = arg0;
    int i;
    unsigned short bit;
    Slot *s;

    c->f122 = 0;
    for (i = 0; i < 16; i++) {
        s = &c->slot[i];
        bit = 1 << i;
        if (c->f164 & bit) {
            if (!(c->f102 & bit)) {
                s->d = 0;
            } else {
                if (c->f120 & bit) {
                    c->f122 = c->f122 | bit;
                    s->d = 1;
                    s->c = s->a;
                } else {
                    int t = s->c - 1;
                    s->c = t;
                    if ((t & 0xFF) == 0) {
                        c->f122 = c->f122 | bit;
                        s->d = 2;
                        s->c = s->b;
                    }
                }
            }
        }
    }
}
