extern char D_0038CC50[];
extern char D_00413F80[];

void func_001BB4D0(int arg0) {
    int *s;
    int t;

    if (D_0038CC50[0] != 0 && !(((int *)D_0038CC50)[0xC] & 1)) {
        s = (int *)D_00413F80;
        if (!((float)arg0 * 100.0f / (float)s[0x16] < 10.0f)) {
            t = (arg0 * 500 / (s[0x16] + 50)) / s[0x2B];
            if (s[0x8D] < t) {
                float f = ((float *)s)[0x8E];

                if (0.2617994f > f) {
                    s[0x8D] = t;
                }
            }
        }
    }
}
