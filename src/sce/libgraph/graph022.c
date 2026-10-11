/* libgraph.a member graph022.o (KFIV; not linked by ICO). sceGsSetHalfOffset2,
   the context-2 twin of graph021.c's sceGsSetHalfOffset: byte-identical code.
   Member number derived; symbol_addrs.txt pins which copy is which. */

void sceGsSetHalfOffset2(void *draw, short x, short y, short half)
{
    unsigned long long v = *(unsigned long long *)((char *)draw + 0x30);
    long long a, b, ta, t, hi;
    b = (short)y;
    b -= (unsigned long long)((int)((v >> 48) & 0x7FF) + 1) >> 1;
    t = b << 4;
    a = (short)x;
    a -= (unsigned long long)((int)((v >> 16) & 0x7FF) + 1) >> 1;
    ta = a << 4;
    if (half != 0)
        hi = (t + 8) << 32;
    else
        hi = b << 36;
    *(long long *)((char *)draw + 0x20) = ta | hi;
}
