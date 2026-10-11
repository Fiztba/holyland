int func_0020B458();
int func_0022B5B8();
int func_0022B8E8();
int sceDevVu0Pause();
int sceDevVu0SetTBit();
int sceDmaReset(int);
int sceGsResetGraph(int, int, int, int);
int sceGsResetPath();
int sceMcInit();
int sceSdRemoteInit();
int sceSifInitIopHeap();

void func_0020AF38(void) {
    func_0020B458();
    sceMcInit();
    func_0022B5B8();
    sceDevVu0Pause();
    func_0022B8E8();
    sceDevVu0SetTBit();
    sceDmaReset(1);
    sceGsResetPath();
    sceGsResetGraph(0, 1, 2, 0);
    sceSdRemoteInit();
    sceSifInitIopHeap();
}
