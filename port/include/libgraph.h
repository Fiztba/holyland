/*
 * port/include/libgraph.h
 *
 * libgraph for the PC port (port/sdk/libgraph.c). The records keep the decomp's layout (they are GS
 * packets: 64-bit register words), with long long spelled int64_t. Functions that take one of the
 * game's own packet types take void *.
 */
#ifndef SCE_LIBGRAPH_LIBGRAPH_H
#define SCE_LIBGRAPH_LIBGRAPH_H

#include <stdint.h>

typedef struct {
    short inter;
    short omode;
    short ffmd;
    short version;
    int intcUsed;
    int handler;
} sceGsGParam;

typedef struct {
    int64_t pmode;   /* 0x00 */
    int64_t smode2;  /* 0x08 */
    int64_t dispfb;  /* 0x10 */
    int64_t display; /* 0x18 */
    int64_t bgcolor; /* 0x20 */
} sceGsDispEnv;

typedef struct {
    int64_t texflush, texflush_addr; /* 0x00 */
    int64_t tex1, tex1_addr;         /* 0x10 */
    int64_t tex0, tex0_addr;         /* 0x20 */
    int64_t clamp, clamp_addr;       /* 0x30 */
} sceGsTexEnv;

typedef struct {
    int64_t frame, frame_addr;           /* 0x00 */
    int64_t zbuf, zbuf_addr;             /* 0x10 */
    int64_t xyoffset, xyoffset_addr;     /* 0x20 */
    int64_t scissor, scissor_addr;       /* 0x30 */
    int64_t prmodecont, prmodecont_addr; /* 0x40 */
    int64_t colclamp, colclamp_addr;     /* 0x50 */
    int64_t dthe, dthe_addr;             /* 0x60 */
    int64_t test, test_addr;             /* 0x70 */
} sceGsDrawEnv;

typedef struct {
    uint64_t test_1, test_1_addr;   /* 0x00 */
    uint64_t prim, prim_addr;       /* 0x10 */
    uint64_t rgbaq, rgbaq_addr;     /* 0x20 */
    uint64_t xyz2_0, xyz2_0_addr;   /* 0x30 */
    uint64_t xyz2_1, xyz2_1_addr;   /* 0x40 */
    uint64_t test_1r, test_1r_addr; /* 0x50 */
} sceGsClear;

typedef struct {
    int64_t giftag0[2];            /* 0x00 */
    int64_t bitbltbuf, abitbltbuf; /* 0x10 */
    int64_t trxpos, atrxpos;       /* 0x20 */
    int64_t trxreg, atrxreg;       /* 0x30 */
    int64_t trxdir, atrxdir;       /* 0x40 */
    int64_t giftag1[2];            /* 0x50 */
} sceGsLoadImage;

typedef struct {
    unsigned int vifcode[4];       /* 0x00 */
    int64_t giftag[2];             /* 0x10 */
    int64_t bitbltbuf, abitbltbuf; /* 0x20 */
    int64_t trxpos, atrxpos;       /* 0x30 */
    int64_t trxreg, atrxreg;       /* 0x40 */
    int64_t finish, afinish;       /* 0x50 */
    int64_t trxdir, atrxdir;       /* 0x60 */
} sceGsStoreImage;

typedef struct {
    sceGsDispEnv disp[2]; /* 0x000 */
    int64_t giftag0[2];   /* 0x050 */
    sceGsDrawEnv draw0;   /* 0x060 */
    sceGsClear clear0;    /* 0x0E0 */
    int64_t giftag1[2];   /* 0x140 */
    sceGsDrawEnv draw1;   /* 0x150 */
    sceGsClear clear1;    /* 0x1D0 */
} sceGsDBuff;

sceGsGParam *sceGsGetGParam(void);
void sceGsResetGraph(short mode, short inter, short omode, short ffmd);
void sceGsResetPath(void);
void sceGsPutDispEnv(void *disp);
int sceGsPutDrawEnv(void *pkt);
void sceGsSetDefDispEnv(sceGsDispEnv *disp, short psm, short w, short h, short dx, short dy);
int sceGsSetDefDrawEnv(void *env, short psm, short w, short h, short ztst, short zpsm);
int sceGsSetDefClear(sceGsClear *cl, short ztst, short x, short y, short w, short h, unsigned char r,
                     unsigned char g, unsigned char b, unsigned char a, unsigned int z);
void sceGsSetDefDBuff(void *db, short psm, short w, short h, short ztst, short zpsm, short flag);
void sceGsSetDefDBuffDc(void *db, short psm, short w, short h, short ztst, short zpsm, short flag);
int sceGsSetDefLoadImage(void *li, short dbp, short dbw, short dpsm, short dsax, short dsay, short rrw,
                         short rrh);
void sceGsSetDefStoreImage(void *si, short sbp, short sbw, short spsm, short ssax, short ssay, short rrw,
                           short rrh);
int sceGsExecLoadImage(void *li, void *image);
int sceGsExecStoreImage(void *si, void *image);
void sceGsSetHalfOffset(void *draw, short x, short y, short half);
void sceGsSetHalfOffset2(void *draw, short x, short y, short half);
int sceGsSwapDBuff(void *db, int id);
int sceGsSwapDBuffDc(void *db, int id);
int sceGsSyncPath(int mode, unsigned short timeout);
int sceGsSyncV(int mode);

#endif /* SCE_LIBGRAPH_LIBGRAPH_H */
