int func_001D0758(int arg0, char *arg1) {
    int n;
    int i;

    *arg1 = arg0 % 10;
    n = arg0 / 10;
    i = 1;
    while (n != 0) {
        arg1[i] = n % 10;
        n = n / 10;
        i++;
    }
    return i;
}
