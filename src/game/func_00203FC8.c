int FlushCache(int);
int func_00102238(char *, int, int);
int func_001024B0(int);
int func_001025C0(int, void *, int);

int func_00203FC8(char *arg0, void *arg1) {
    int temp_v0;

    temp_v0 = func_00102238(arg0, 0, 1);
    if (temp_v0 < 0) {
        return 0;
    }
    func_001025C0(temp_v0, arg1, -1);
    func_001024B0(temp_v0);
    FlushCache(0);
    return (int)arg1;
}
