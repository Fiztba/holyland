int func_0021F098(long, long, long);

int func_0021F3F0(long arg0, unsigned int arg1, unsigned int arg2, unsigned int arg3, unsigned long arg4) {
    unsigned long lo = arg1 | (arg4 << 48);
    unsigned long hi = ((unsigned long)arg3 << 32) | ((unsigned long)arg2 << 16);
    return func_0021F098(arg0, 0x40, lo | hi);
}
