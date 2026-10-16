/* libsdr.a (SDK 2.2.0; KFIV, not linked by ICO, which uses libsndn2). The
 * archive's member boundaries in this build are not known, so this file is
 * the archive's whole run. */
#include <stdio.h>
#include <eekernel.h>
#include <sifrpc.h>

/* The SIF RPC server entry points this member calls, declared here: the
 * signatures of their definitions in sce/libkernl/sifrpc.c. */
void sceSifRegisterRpc(sceSifServeData *sd, unsigned int sid, sceSifRpcFunc func, void *buff,
                       sceSifRpcFunc cfunc, void *cbuff, sceSifQueueData *queue);
void sceSifSetRpcQueue(sceSifQueueData *qd, int key);
void sceSifRpcLoop(sceSifQueueData *qd);

/* the variable argument list of sceSdRemote: the register save area the
 * compiler spills a1..a7 into, eight bytes per argument */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)__builtin_next_arg(last) - 0x38)
#define va_arg(ap, type) (*(type *)((ap += 8) - 8))

/* The member's .data in link order: the build stamp, the end function
 * sceSdRemote hands a synchronous call, and the callback slots the
 * registration calls fill. */
static const char sdrErr[16] = "error \n"; /* derived name */

static const char sdrCant[48] = "Can't start thread for streaming.\n"; /* derived name */

static char sdrVersion[16] = "PsIIlibsdr  2200"; /* derived name */

static void *sdrEndFunc = 0; /* derived name */

static int sdrCbA = 0; /* derived name */ /* 0x01 callback, arg word 0 == 0 */
static int sdrCbB = 0; /* derived name */ /* 0x02 callback, arg word 0 != 0 */
static int sdrCbC = 0; /* derived name */ /* 0x03 callback */
static int sdrCbD = 0; /* derived name */ /* 0x0B callback, arg word 0 == 0 */
static int sdrCbE = 0; /* derived name */ /* 0x0C callback, arg word 0 != 0 */
static int sdrCbF = 0; /* derived name */ /* 0x0D callback */
static int sdrCbG = 0; /* derived name */ /* 0x0B argument, arg word 0 == 0 */
static int sdrCbH = 0; /* derived name */ /* 0x0B argument, arg word 0 != 0 */
static int sdrCbI = 0; /* derived name */ /* 0x0D argument */

/* The bss in link order: an eight-byte-aligned block nothing reads, the
 * DMA descriptors sceSdTransToIOP fills, the request record sceSdRemote
 * builds (its first word points back at the record, the arguments follow,
 * and the reply lands on it), the SIF client record, and the streaming
 * thread's stack and receive buffer. */
static char sdrCtl[0x18]; /* derived name */

static sceSifDmaData sdrDma[20]; /* derived name */

typedef struct { /* derived name */
    void *self;
    int arg[15];
} SdrReq; /* derived name */

static SdrReq sdrReq; /* derived name */

/* the SIF client record, in a 64-byte slot */
static union { /* derived name */
    sceSifRpcClientData cd;
    char pad[0x40];
} sdrClientSlot; /* derived name */

#define sdrClient (sdrClientSlot.cd)

static char sdrStack[0x1100]; /* derived name */

static char sdrRecv[0x40]; /* derived name */

extern char _gp[];

static void sdrThread(void *arg);
static void *sdrCallBack(int fno, void *buff, int size);

/* the R5900 nop the wait loops and the hang loop spell out */
#define NOP() __asm__ __volatile__("nop")

int sceSdRemoteInit(void)
{
    int cnt;

    sceSifInitRpc(0);
    do {
        if (sceSifBindRpc(&sdrClient, 0x80000701, 0) < 0) {
            printf(sdrErr);
            for (;;) {
                NOP();
                NOP();
                NOP();
                NOP();
                NOP();
            }
        }
        cnt = 0x2710;
        while (cnt != -1) {
            cnt--;
            NOP();
            NOP();
            NOP();
            NOP();
        }
    } while (sdrClient.serve == 0);
    FlushCache(0);
    return 1;
}

int sceSdTransToIOP(unsigned int src, unsigned int dest, int size, int wait)
{
    int id;

    sdrDma[0].src = src;
    sdrDma[0].dest = dest;
    sdrDma[0].size = size;
    sdrDma[0].u.attr = 0;
    id = sceSifSetDma(sdrDma, 1);
    if (wait == 1) {
        while (sceSifDmaStat(id) >= 0) {
        }
    }
    return id != 0 ? 0 : -1;
}

void sceSdRemoteSetEndFunc(void *fn)
{
    sdrEndFunc = fn;
}

int sceSdRemote(int mode, ...)
{
    va_list ap;
    int cmd;
    int flag;
    void *endf;
    int ret;
    int i;
    char unused[16]; /* never read: reproduces the retail frame (0xD0) */

    va_start(ap, mode);
    endf = 0;
    sdrReq.self = &sdrReq;
    cmd = va_arg(ap, int);
    for (i = 0; i < 6; i++) {
        sdrReq.arg[i] = va_arg(ap, int);
    }
    flag = 0;
    if (mode == 0) {
        flag = 1;
        endf = sdrEndFunc;
    }
    if (cmd == 0x8110) {
        if (sdrReq.arg[0] == 0) {
            sdrCbA = sdrReq.arg[1];
        } else {
            sdrCbB = sdrReq.arg[1];
        }
    } else if (cmd == 0x8120) {
        sdrCbC = sdrReq.arg[0];
    } else if (cmd == 0x8160) {
        if (sdrReq.arg[0] == 0) {
            sdrCbD = sdrReq.arg[1];
            sdrCbG = sdrReq.arg[2];
        } else {
            sdrCbE = sdrReq.arg[1];
            sdrCbH = sdrReq.arg[2];
        }
    } else if (cmd == 0x8170) {
        sdrCbF = sdrReq.arg[0];
        sdrCbI = sdrReq.arg[1];
    }
    if (cmd == 0x8130) {
        sceSifCallRpc(&sdrClient, sdrReq.arg[0] | 0x8130, flag, (void *)sdrReq.arg[1], 0x40, 0, 0,
                      endf, sdrReq.self);
        ret = *(int *)&sdrReq;
    } else if (cmd == 0x8140) {
        sceSifCallRpc(&sdrClient, sdrReq.arg[0] | 0x8140, flag, &sdrReq, 0x40,
                      (void *)sdrReq.arg[1], 0x40, endf, (void *)sdrReq.arg[1]);
    } else {
        sceSifCallRpc(&sdrClient, cmd, flag, &sdrReq, 0x40, &sdrReq, 0x10, endf, &sdrReq);
        ret = *(int *)&sdrReq;
    }
    return ret;
}

int sceSdRemoteCallbackInit(int priority)
{
    struct ThreadParam tp;

    sdrCbA = 0;
    sdrCbB = 0;
    sdrCbC = 0;
    sdrCbD = 0;
    sdrCbE = 0;
    sdrCbF = 0;
    sdrCbG = 0;
    sdrCbH = 0;
    sdrCbI = 0;
    sceSdRemote(1, 0xE620);
    tp.entry = sdrThread;
    tp.stack = sdrStack;
    tp.stackSize = 0x1000;
    tp.initPriority = priority;
    tp.gpReg = _gp;
    tp.option = CreateThread(&tp);
    if (StartThread(tp.option, 0) < 0) {
        printf(sdrCant);
        return -1;
    }
    return tp.option;
}

static void sdrThread(void *arg)
{
    sceSifQueueData qd;
    char sd[0x50]; /* derived name: the serve record, in a 0x50-byte slot */

    sceSifInitRpc(0);
    sceSifSetRpcQueue(&qd, GetThreadId());
    sceSifRegisterRpc((sceSifServeData *)sd, 0x80000704, sdrCallBack, sdrRecv, 0, 0, &qd);
    sceSifRpcLoop(&qd);
}

/* the RPC server the streaming thread runs: the command in the request
   buffer's first word picks a callback slot to run */
static void *sdrCallBack(int fno, void *buff, int size)
{
    switch (*(int *)buff) {
    case 1:
        if (sdrCbA) {
            ((void (*)(void))sdrCbA)();
        }
        break;
    case 2:
        if (sdrCbB) {
            ((void (*)(void))sdrCbB)();
        }
        break;
    case 3:
        if (sdrCbC) {
            ((void (*)(void))sdrCbC)();
        }
        break;
    case 11:
        if (sdrCbD) {
            ((void (*)(int, int))sdrCbD)(0, sdrCbG);
        }
        break;
    case 12:
        if (sdrCbE) {
            ((void (*)(int, int))sdrCbE)(1, sdrCbH);
        }
        break;
    case 13:
        if (sdrCbF) {
            ((void (*)(int, int))sdrCbF)(((int *)buff)[1], sdrCbI);
        }
        break;
    }
    return 0;
}

/* derived name */
static const int sdrPadB = 0;
