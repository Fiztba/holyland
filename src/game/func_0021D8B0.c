int WaitSema(int);
int SignalSema(int);

int func_0021D8B0(int *arg0) {
    int temp_s1;

    WaitSema(arg0[0x40 / 4]);
    temp_s1 = (arg0[0x10 / 4] << 11) + arg0[0x14 / 4];
    SignalSema(arg0[0x40 / 4]);
    return temp_s1;
}
