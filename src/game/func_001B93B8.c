extern char D_00413F80[];
extern char D_00414280[];

void func_00216C48(void *, void *, void *);

void func_001B93B8(char *arg0) {
    func_00216C48(D_00413F80, D_00413F80, arg0);
    func_00216C48(D_00414280, D_00414280, arg0);
    *(float *)(D_00413F80 + 0x1D8) = *(float *)(D_00413F80 + 0x1D8) + *(float *)(arg0 + 4);
    *(float *)(D_00413F80 + 0x1DC) = *(float *)(D_00413F80 + 0x1DC) + *(float *)(arg0 + 4);
    *(float *)(D_00413F80 + 0x214) = *(float *)(D_00413F80 + 0x214) + *(float *)(arg0 + 4);
    *(float *)(D_00414280 + 0x1D8) = *(float *)(D_00414280 + 0x1D8) + *(float *)(arg0 + 4);
    *(float *)(D_00414280 + 0x1DC) = *(float *)(D_00414280 + 0x1DC) + *(float *)(arg0 + 4);
    *(float *)(D_00414280 + 0x214) = *(float *)(D_00414280 + 0x214) + *(float *)(arg0 + 4);
}
