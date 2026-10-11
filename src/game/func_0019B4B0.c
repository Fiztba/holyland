void *memset(void *, int, unsigned int);
int FlushCache(int);
typedef struct {
    long long frame, frame_addr, zbuf, zbuf_addr, xyoffset, xyoffset_addr;
    long long scissor, scissor_addr, prmodecont, prmodecont_addr;
    long long colclamp, colclamp_addr, dthe, dthe_addr, test, test_addr;
} Env;
typedef struct { long long a, b, c, d; } Blk;
typedef struct { int tag[4]; Env env; } Pkt;
int sceGsPutDrawEnv(void *);
int sceGsSetDefDrawEnv(Env *, short, short, short, short, short);
int sceGsSyncPath(int, unsigned short);
extern Blk D_0043EB90;
typedef struct { char hdr[16]; Blk blk; Pkt pkt; } Frame;

void func_0019B4B0(void) {
    Frame f;
    Pkt *p;
    memset(&f, 0, 0x10);
    f.blk = D_0043EB90;
    p = &f.pkt;
    f.pkt.tag[0] = 0x8008;
    f.pkt.tag[1] = 0x10000000;
    f.pkt.tag[2] = 0xE;
    f.pkt.tag[3] = 0;
    sceGsSetDefDrawEnv(&f.pkt.env, 0xA, 0x100, 0x100, 0, 0);
    f.pkt.env.scissor = (((long long)0xFF0000 << 16) | 0xFF) << 16;
    f.pkt.env.frame = 0x0A0401C2;
    FlushCache(0);
    sceGsPutDrawEnv(p);
    sceGsSyncPath(0, 0);
}
