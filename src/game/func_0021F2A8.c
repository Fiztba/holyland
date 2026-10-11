int func_0021F098(long, long, long);

int func_0021F2A8(long arg0, unsigned int arg1, unsigned int arg2, unsigned int arg3, unsigned int arg4, unsigned long arg5) {
    unsigned long lo = ((unsigned long)arg3 << 16) | ((unsigned long)arg2 << 8);
    unsigned long hi = arg1 | (arg5 << 32);
    return func_0021F098(arg0, 1, hi | lo | ((unsigned long)arg4 << 24));
}
