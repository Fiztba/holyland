void sceGsSetHalfOffset(void *draw, short x, short y, short half);
int sceGsSwapDBuff(void *db, int id);
int sceGsSyncV(int mode);
void FlushCache(int operation);

extern char D_0042C4C0[];
extern char D_0042C520[];
extern char D_0042C610[];
extern int D_00446E88;

void func_00209D68(void) {
    int odev = !sceGsSyncV(0);

    sceGsSetHalfOffset(D_00446E88 == 1 ? D_0042C610 : D_0042C520, 0x800, 0x800, odev);
    FlushCache(0);
    sceGsSwapDBuff(D_0042C4C0, D_00446E88);
    D_00446E88 = 1 - D_00446E88;
}
