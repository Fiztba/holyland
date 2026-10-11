int FlushCache(int);
int sceClose(int);
int sceOpen(const char *, int);
int sceWrite(int, const void *, int);
int sprintf(char *, const char *, ...);
extern char D_0043EC50[];

void func_0019C228(void *arg0, int arg1) {
    char name[256];
    int i;
    int fd;
    int ok;

    i = 0;
    do {
        ok = i < 0x400;
        if (!ok) {
            break;
        }
        sprintf(name, D_0043EC50, i);
        fd = sceOpen(name, 2);
        if (fd < 0) {
            break;
        }
        sceClose(fd);
        i++;
    } while (1);
    if (ok) {
        FlushCache(0);
        fd = sceOpen(name, 0x202);
        if (fd >= 0) {
            sceWrite(fd, arg0, arg1);
            sceClose(fd);
        }
    }
}
