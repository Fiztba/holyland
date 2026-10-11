void FlushCache(int);
int sceSifSetDma(void *, int);
int sceSifDmaStat(int);

typedef struct {
    void *data;
    void *addr;
    int size;
    int attr;
} SifDmaData;

int func_0021CBE8(void *arg0, void *arg1, int arg2) {
    SifDmaData dma;
    int temp_s0;

    if (arg2 <= 0) {
        return 0;
    }
    dma.data = arg1;
    dma.addr = arg0;
    dma.size = arg2;
    dma.attr = 0;
    FlushCache(0);
    temp_s0 = sceSifSetDma(&dma, 1);
    do {
    } while (sceSifDmaStat(temp_s0) >= 0);
    return arg2;
}
