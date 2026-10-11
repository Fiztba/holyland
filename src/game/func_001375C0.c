extern float D_00444F74;
extern float D_00444F78;
void func_00195388(float, void *, float);

void func_001375C0(void *arg0) {
    float f0;
    float f12;
    char *s;
    char *obj;
    char *ent;
    unsigned short flags;
    unsigned short n;
    int i;

    f0 = D_00444F74;
    flags = *(unsigned short *)((char *)arg0 + 0xC8);
    s = (char *)arg0 + 0xE0;
    if (flags & 0x200) {
        f12 = f0;
    } else {
        f12 = (flags & 0x100) ? -f0 : 0.0f;
        f0 = D_00444F78;
    }
    if (f0 < f12 || (f0 = -f0, f12 < f0)) {
        f12 = f0;
    }
    func_00195388(f12, s + 0x4C, 20.0f);
    obj = *(char **)arg0;
    n = *(unsigned short *)(obj + 0xF0);
    ent = *(char **)(obj + 0xF8);
    for (i = 0; i < n; i++) {
        if (i == 0x17 || i == 0x1C || i == 0x1F || i == 0x20) {
            *(float *)(ent + 0x54) = -*(float *)(s + 0x4C) * 0.5f;
        }
        ent += 0x80;
    }
}
