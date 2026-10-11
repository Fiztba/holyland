extern char D_00437838[];
extern char D_00442178[];

void func_0021B9E0(void);
void func_0021BDD8(char *);
int func_0021DFD0(int);
void func_0021E5A0(char *);
int func_0021E610(char *);
int sceMpegGetPicture(int, int, int);
int sceMpegIsEnd(int);
void sceMpegReset(int);

int func_0021E208(int arg0) {
    int ret = 1;
    int pic;

    while (!sceMpegIsEnd(arg0)) {
        if (func_0021DFD0(arg0) == 1) {
            ret = -1;
            break;
        }
        while ((pic = func_0021E610(D_00437838)) == 0) {
            func_0021B9E0();
        }
        if (sceMpegGetPicture(arg0, pic, 0x460) < 0) {
            func_0021BDD8(D_00442178);
        }
        func_0021E5A0(D_00437838);
        func_0021B9E0();
    }
    sceMpegReset(arg0);
    return ret;
}
