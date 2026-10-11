unsigned int *func_00109990(int);
void func_00109650(unsigned int *, unsigned int *);

void func_00116B60(int *arg0) {
    unsigned int *p0;
    unsigned int *p;

    if (arg0 != 0) {
        p0 = p = func_00109990(2);
        *p++ = *arg0 | 0x30000000;
        *p++ = (unsigned int)arg0 + 0x10;
        *p++ = 0;
        *p++ = 0;
        func_00109650(p0, p);
    }
}
