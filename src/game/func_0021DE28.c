void sceMpegCreate(int);
void sceMpegAddCallback(int, int, void (*)(void), int);
int func_0021DF78(int);
void func_0021CE48(int, int, int, int, int, int);
void func_0021E2D0(void);
void func_0021E2D8(void);
void func_0021E310(void);
void func_0021E338(void);
void func_0021E360(void);
void func_0021E380(void);

int func_0021DE28(int arg0, int a1, int a2, int arg3, int arg4, int arg5, int arg6, int arg7) {
    sceMpegCreate(arg0);
    sceMpegAddCallback(arg0, 0, func_0021E2D0, 0);
    sceMpegAddCallback(arg0, 1, func_0021E2D8, 0);
    sceMpegAddCallback(arg0, 2, func_0021E310, 0);
    sceMpegAddCallback(arg0, 3, func_0021E338, 0);
    sceMpegAddCallback(arg0, 4, func_0021E360, 0);
    sceMpegAddCallback(arg0, 5, func_0021E380, 0);
    func_0021DF78(arg0);
    func_0021CE48(arg0 + 0x48, arg3, arg4, arg5, arg6, arg7);
    return 1;
}
