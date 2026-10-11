int func_0021F098(long, long, long);

int func_0021F0E8(long arg0, unsigned int arg1, unsigned int arg2, unsigned int arg3, unsigned int arg4, unsigned int arg5, unsigned int arg6, unsigned long arg7) {
    unsigned long b = ((unsigned long) arg1) | (arg7 << 32);
    unsigned long a = (((unsigned long) arg2) << 2) | (((unsigned long) arg5) << 9);
    unsigned long c = ((((unsigned long) arg4) << 6) | (((unsigned long) arg3) << 5)) | (((unsigned long) arg6) << 19);

    return func_0021F098(arg0, 0x14, (b | a) | c);
}
