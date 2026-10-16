/* libkernl.a member memclr.o (KFIV: not in ICO) */
void memclr(void *p, int n)
{
    unsigned char *c = p;
    int i;
    for (i = n - 1; i != -1; i--) {
        *c++ = 0;
    }
}
