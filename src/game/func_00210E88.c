typedef struct {
    int type;
    char pad[0xA0];
} Entry;

typedef struct {
    char pad[0xC];
    Entry entries[16];
} Owner;

typedef void (*Handler)(Entry *);

extern Handler D_004331B0[];
extern Handler D_00433440[];
extern Handler D_00433650[];

int func_00210E88(Owner *arg0) {
    int i;
    int n;
    int type;

    n = 0;
    for (i = 0; i < 16; i++) {
        type = arg0->entries[i].type;
        if (type != 0) {
            if (!(type & 0x8000)) {
                if (type >= 0xC8) {
                    D_00433650[type - 0xC8](&arg0->entries[i]);
                } else if (type >= 0x64) {
                    D_00433440[type - 0x64](&arg0->entries[i]);
                } else {
                    D_004331B0[type](&arg0->entries[i]);
                }
            }
            n++;
        }
    }
    return n;
}
