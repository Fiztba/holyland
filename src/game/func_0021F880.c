int sceCdDiskReady(int);
int sceClose(int);
int sceLseek(int, int, int);
int sceOpen(const char *, int);

int func_0021F880(const char *arg0, int *arg1) {
    int fd;

    if ((arg0[0] == 0x63 || arg0[0] == 0x43) &&
        (arg0[1] == 0x64 || arg0[1] == 0x44) &&
        (arg0[2] == 0x72 || arg0[2] == 0x52) &&
        (arg0[3] == 0x6F || arg0[3] == 0x4F) &&
        (arg0[4] == 0x6D || arg0[4] == 0x4D)) {
        sceCdDiskReady(0);
        fd = sceOpen(arg0, 0);
    } else {
        fd = sceOpen(arg0, 1);
    }
    if (fd >= 0) {
        *arg1 = sceLseek(fd, 0, 2);
        if (*arg1 < 0 || sceLseek(fd, 0, 0) < 0) {
            sceClose(fd);
            return -1;
        }
        return fd;
    }
    return fd;
}
