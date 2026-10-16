/* The Sony libraries the game calls, as step-0 stand-ins: each does nothing (or the least that keeps
   the game going) and logs once that it was reached. Step 1 gives them their PC behaviour; graphics
   and sound replace the libgraph, libdma, libsdr and SIF ones. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eekernel.h"
#include "libcdvd.h"
#include "libdev.h"
#include "libdma.h"
#include "libgraph.h"
#include "libmc.h"
#include "libmpeg.h"
#include "libpad.h"
#include "libsdr.h"
#include "port/host.h"
#include "sifdev.h"
#include "sifrpc.h"

#define STUB(name) PORT_TODO(name " (stub)")

/* ---- EE kernel ------------------------------------------------------------------------------ */
int AddIntcHandler(int cause, int (*handler)(int), int next) { (void)cause; (void)handler; (void)next; STUB("AddIntcHandler"); return 1; }
int RemoveIntcHandler(int cause, int id) { (void)cause; (void)id; return 0; }
int AddDmacHandler(int channel, int (*handler)(int), int next) { (void)channel; (void)handler; (void)next; STUB("AddDmacHandler"); return 1; }
int RemoveDmacHandler(int channel, int id) { (void)channel; (void)id; return 0; }
int EnableIntc(int cause) { (void)cause; return 1; }
int DisableIntc(int cause) { (void)cause; return 1; }
int EnableDmac(int channel) { (void)channel; return 1; }
int DisableDmac(int channel) { (void)channel; return 1; }
int iEnableIntc(int cause) { (void)cause; return 1; }
int iDisableIntc(int cause) { (void)cause; return 1; }
int iEnableDmac(int channel) { (void)channel; return 1; }
int iDisableDmac(int channel) { (void)channel; return 1; }
int DIntr(void) { return 1; }
int EIntr(void) { return 1; }
int CreateThread(struct ThreadParam *param) { (void)param; STUB("CreateThread"); return 2; }
int DeleteThread(int id) { (void)id; return 0; }
int StartThread(int id, void *arg) { (void)id; (void)arg; STUB("StartThread"); return 0; }
void ExitThread(void) { STUB("ExitThread"); }
int TerminateThread(int id) { (void)id; STUB("TerminateThread"); return 0; }
void Exit(int status) { fprintf(stderr, "[port] the game called Exit(%d)\n", status); exit(status); }
int ChangeThreadPriority(int id, int priority) { (void)id; (void)priority; return 0; }
int RotateThreadReadyQueue(int priority) { (void)priority; return 0; }
int GetThreadId(void) { return 1; }
int SleepThread(void) { STUB("SleepThread"); return 0; }
int WakeupThread(int id) { (void)id; return 0; }
int iWakeupThread(int id) { (void)id; return 0; }
int CreateSema(struct SemaParam *param) { (void)param; return 1; }
int DeleteSema(int sema) { (void)sema; return 0; }
int SignalSema(int sema) { (void)sema; return 0; }
int iSignalSema(int sema) { (void)sema; return 0; }
int WaitSema(int sema) { (void)sema; return 0; }
int PollSema(int sema) { (void)sema; return 0; }
void FlushCache(int operation) { (void)operation; }
void SyncDCache(void *start, void *end) { (void)start; (void)end; }
void iSyncDCache(void *start, void *end) { (void)start; (void)end; }
void InvalidDCache(void *start, void *end) { (void)start; (void)end; }

/* ---- libgraph ------------------------------------------------------------------------------- */
static sceGsGParam gs_param = {1, 2, 0, 0, 0, 0};
sceGsGParam *sceGsGetGParam(void) { return &gs_param; }
void sceGsResetGraph(short mode, short inter, short omode, short ffmd) { (void)mode; gs_param.inter = inter; gs_param.omode = omode; gs_param.ffmd = ffmd; }
void sceGsResetPath(void) {}
void sceGsPutDispEnv(void *disp) { (void)disp; STUB("sceGsPutDispEnv"); }
int sceGsPutDrawEnv(void *pkt) { (void)pkt; STUB("sceGsPutDrawEnv"); return 0; }
void sceGsSetDefDispEnv(sceGsDispEnv *disp, short psm, short w, short h, short dx, short dy) { (void)psm; (void)w; (void)h; (void)dx; (void)dy; memset(disp, 0, sizeof(*disp)); STUB("sceGsSetDefDispEnv"); }
int sceGsSetDefDrawEnv(void *env, short psm, short w, short h, short ztst, short zpsm) { (void)env; (void)psm; (void)w; (void)h; (void)ztst; (void)zpsm; STUB("sceGsSetDefDrawEnv"); return 8; }
int sceGsSetDefClear(sceGsClear *cl, short ztst, short x, short y, short w, short h, unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned int z) { (void)cl; (void)ztst; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a; (void)z; STUB("sceGsSetDefClear"); return 6; }
void sceGsSetDefDBuff(void *db, short psm, short w, short h, short ztst, short zpsm, short flag) { (void)db; (void)psm; (void)w; (void)h; (void)ztst; (void)zpsm; (void)flag; STUB("sceGsSetDefDBuff"); }
void sceGsSetDefDBuffDc(void *db, short psm, short w, short h, short ztst, short zpsm, short flag) { (void)db; (void)psm; (void)w; (void)h; (void)ztst; (void)zpsm; (void)flag; STUB("sceGsSetDefDBuffDc"); }
int sceGsSetDefLoadImage(void *li, short dbp, short dbw, short dpsm, short dsax, short dsay, short rrw, short rrh) { (void)li; (void)dbp; (void)dbw; (void)dpsm; (void)dsax; (void)dsay; (void)rrw; (void)rrh; STUB("sceGsSetDefLoadImage"); return 0; }
void sceGsSetDefStoreImage(void *si, short sbp, short sbw, short spsm, short ssax, short ssay, short rrw, short rrh) { (void)si; (void)sbp; (void)sbw; (void)spsm; (void)ssax; (void)ssay; (void)rrw; (void)rrh; STUB("sceGsSetDefStoreImage"); }
int sceGsExecLoadImage(void *li, void *image) { (void)li; (void)image; STUB("sceGsExecLoadImage"); return 0; }
int sceGsExecStoreImage(void *si, void *image) { (void)si; (void)image; STUB("sceGsExecStoreImage"); return 0; }
void sceGsSetHalfOffset(void *draw, short x, short y, short half) { (void)draw; (void)x; (void)y; (void)half; }
void sceGsSetHalfOffset2(void *draw, short x, short y, short half) { (void)draw; (void)x; (void)y; (void)half; }
int sceGsSwapDBuff(void *db, int id) { (void)db; (void)id; STUB("sceGsSwapDBuff"); return 0; }
int sceGsSwapDBuffDc(void *db, int id) { (void)db; (void)id; STUB("sceGsSwapDBuffDc"); return 0; }
int sceGsSyncPath(int mode, unsigned short timeout) { (void)mode; (void)timeout; return 0; }
int sceGsSyncV(int mode) { (void)mode; STUB("sceGsSyncV (no frame pacing yet)"); return 0; }

/* ---- libdma --------------------------------------------------------------------------------- */
static DmaChan dma_channels[10];
DmaChan *sceDmaGetChan(unsigned int id) { return id < 10 ? &dma_channels[id] : NULL; }
int sceDmaReset(int mode) { (void)mode; return 0; }
void sceDmaSend(DmaChan *channel, void *addr) { (void)channel; (void)addr; STUB("sceDmaSend (nothing draws yet)"); }
void sceDmaSendN(DmaChan *channel, void *addr, int qwc) { (void)channel; (void)addr; (void)qwc; STUB("sceDmaSendN"); }
int sceDmaSync(DmaChan *channel, int mode, int timeout) { (void)channel; (void)mode; (void)timeout; return 0; }

/* ---- libpad --------------------------------------------------------------------------------- */
int scePadInit(int mode) { (void)mode; return 1; }
int scePadPortOpen(int port, int slot, void *dma_buffer) { (void)port; (void)slot; (void)dma_buffer; return 1; }
int scePadRead(int port, int slot, void *data) { (void)port; (void)slot; memset(data, 0xFF, 32); ((unsigned char *)data)[0] = 0; ((unsigned char *)data)[1] = 0x79; STUB("scePadRead (no input yet)"); return 32; }
int scePadGetState(int port, int slot) { (void)port; (void)slot; return scePadStateStable; }
int scePadGetReqState(int port, int slot) { (void)port; (void)slot; return 0; }
int scePadInfoMode(int port, int slot, int term, int index) { (void)port; (void)slot; (void)term; (void)index; return 7; }
int scePadSetMainMode(int port, int slot, int mode, int lock) { (void)port; (void)slot; (void)mode; (void)lock; return 1; }
int scePadInfoAct(int port, int slot, int actno, int term) { (void)port; (void)slot; (void)actno; (void)term; return 0; }
int scePadSetActAlign(int port, int slot, const unsigned char *data) { (void)port; (void)slot; (void)data; return 1; }
int scePadSetActDirect(int port, int slot, const unsigned char *data) { (void)port; (void)slot; (void)data; return 1; }
int scePadInfoPressMode(int port, int slot) { (void)port; (void)slot; return 1; }
int scePadEnterPressMode(int port, int slot) { (void)port; (void)slot; return 1; }
int scePadExitPressMode(int port, int slot) { (void)port; (void)slot; return 1; }

/* ---- libmc: no card in either slot ---------------------------------------------------------- */
static int mc_last_cmd, mc_last_result;
#define MC_CALL(cmd, result) (mc_last_cmd = (cmd), mc_last_result = (result), 0)
int sceMcInit(void) { return 0; }
int sceMcGetInfo(int port, int slot, int *type, int *free, int *format) { (void)port; (void)slot; if (type) *type = 0; if (free) *free = 0; if (format) *format = 0; STUB("sceMcGetInfo (no memory card yet)"); return MC_CALL(1, -10); }
int sceMcOpen(int port, int slot, const char *name, int mode) { (void)port; (void)slot; (void)name; (void)mode; return MC_CALL(2, -4); }
int sceMcClose(int fd) { (void)fd; return MC_CALL(3, -4); }
int sceMcRead(int fd, void *buffer, int size) { (void)fd; (void)buffer; (void)size; return MC_CALL(5, -4); }
int sceMcWrite(int fd, const void *buffer, int size) { (void)fd; (void)buffer; (void)size; return MC_CALL(6, -4); }
int sceMcSeek(int fd, int offset, int origin) { (void)fd; (void)offset; (void)origin; return MC_CALL(4, -4); }
int sceMcMkdir(int port, int slot, const char *name) { (void)port; (void)slot; (void)name; return MC_CALL(11, -4); }
int sceMcChdir(int port, int slot, const char *new_dir, char *old_dir) { (void)port; (void)slot; (void)new_dir; (void)old_dir; return MC_CALL(12, -4); }
int sceMcGetDir(int port, int slot, const char *name, unsigned int mode, int max_entries, sceMcTblGetDir *table) { (void)port; (void)slot; (void)name; (void)mode; (void)max_entries; (void)table; return MC_CALL(13, -4); }
int sceMcFormat(int port, int slot) { (void)port; (void)slot; return MC_CALL(16, -4); }
int sceMcDelete(int port, int slot, const char *name) { (void)port; (void)slot; (void)name; return MC_CALL(15, -4); }
int sceMcSync(int mode, int *cmd, int *result) { (void)mode; if (mc_last_cmd == 0) return -1; if (cmd) *cmd = mc_last_cmd; if (result) *result = mc_last_result; mc_last_cmd = 0; return 1; }

/* ---- libmpeg: every movie is over at once --------------------------------------------------- */
void sceMpegInit(void) {}
int sceMpegCreate(sceMpeg *mp, void *work_area, int size) { (void)work_area; (void)size; memset(mp, 0, sizeof(*mp)); STUB("sceMpegCreate (no movies yet)"); return 1; }
int sceMpegDelete(sceMpeg *mp) { (void)mp; return 0; }
void sceMpegReset(sceMpeg *mp) { (void)mp; }
sceMpegCallback sceMpegAddCallback(sceMpeg *mp, int type, sceMpegCallback func, void *data) { (void)mp; (void)type; (void)data; return func; }
int sceMpegAddStrCallback(sceMpeg *mp, int type, int ch, sceMpegCallback func, void *data) { (void)mp; (void)type; (void)ch; (void)func; (void)data; return 0; }
int sceMpegDemuxPssRing(sceMpeg *mp, void *buf, int size, void *ring, int ring_size) { (void)mp; (void)buf; (void)ring; (void)ring_size; return size; }
int sceMpegGetPicture(sceMpeg *mp, void *rgb32, int mbcount) { (void)mp; (void)rgb32; (void)mbcount; return -1; }
int sceMpegIsEnd(sceMpeg *mp) { (void)mp; return 1; }
int sceMpegIsRefBuffEmpty(sceMpeg *mp) { (void)mp; return 1; }
void sceMpegSetDecodeMode(sceMpeg *mp, int ni, int np, int nb) { (void)mp; (void)ni; (void)np; (void)nb; }

/* ---- libcdvd -------------------------------------------------------------------------------- */
int sceCdInit(int mode) { (void)mode; return 1; }
int sceCdMmode(int media) { (void)media; return 1; }
int sceCdDiskReady(int mode) { (void)mode; return 2; }
int sceCdSync(int mode) { (void)mode; return 0; }
int sceCdGetError(void) { return 0; }
int sceCdBreak(void) { return 1; }
int sceCdSearchFile(sceCdlFILE *file, const char *name) { (void)file; (void)name; STUB("sceCdSearchFile (no disc yet)"); return 0; }
int sceCdRead(unsigned int lsn, unsigned int sectors, void *buf, sceCdRMode *mode) { (void)lsn; (void)sectors; (void)buf; (void)mode; STUB("sceCdRead (no disc yet)"); return 0; }
int sceCdSeek(unsigned int lsn) { (void)lsn; return 1; }
int sceCdReadClock(sceCdCLOCK *clock) { memset(clock, 0, sizeof(*clock)); STUB("sceCdReadClock"); return 1; }
int sceCdStInit(unsigned int bufmax, unsigned int bankmax, void *buffer) { (void)bufmax; (void)bankmax; (void)buffer; return 1; }
int sceCdStStart(unsigned int lsn, sceCdRMode *mode) { (void)lsn; (void)mode; return 1; }
int sceCdStRead(unsigned int sectors, void *buffer, unsigned int mode, unsigned int *error) { (void)sectors; (void)buffer; (void)mode; if (error) *error = 0; return 0; }
int sceCdStSeek(unsigned int lsn) { (void)lsn; return 1; }
int sceCdStStop(void) { return 1; }

/* ---- Files and the IOP ---------------------------------------------------------------------- */
int sceOpen(const char *name, int flags, ...) { (void)name; (void)flags; STUB("sceOpen (no files yet)"); return -1; }
int sceClose(int fd) { (void)fd; return 0; }
int sceRead(int fd, void *buffer, int size) { (void)fd; (void)buffer; (void)size; return -1; }
int sceWrite(int fd, const void *buffer, int size) { (void)fd; (void)buffer; return size; }
int sceLseek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
int sceFsReset(void) { return 0; }
int sceSifRebootIop(const char *image) { (void)image; return 1; }
int sceSifSyncIop(void) { return 1; }
int sceSifLoadModule(const char *path, int arg_length, const char *args) { (void)path; (void)arg_length; (void)args; return 1; }
static int iop_heap_next = 0x100000;
int sceSifInitIopHeap(void) { return 0; }
int sceSifAllocIopHeap(int size) { int address = iop_heap_next; iop_heap_next += (size + 0xFF) & ~0xFF; return address; }
int sceSifFreeIopHeap(int iop_address) { (void)iop_address; return 0; }
int sceSifLoadIopHeap(const char *path, int iop_address) { (void)path; (void)iop_address; STUB("sceSifLoadIopHeap"); return 0; }
void sceSifInitRpc(int mode) { (void)mode; }
int sceSifBindRpc(sceSifClientData *cd, unsigned int server_id, int mode) { (void)mode; memset(cd, 0, sizeof(*cd)); cd->command = server_id; cd->server = cd; return 0; }
int sceSifCallRpc(sceSifClientData *cd, unsigned int function, unsigned int mode, void *send_buffer, int send_size, void *receive_buffer, int receive_size, sceSifEndFunc end_function, void *end_param) {
    (void)cd; (void)function; (void)mode; (void)send_buffer; (void)send_size;
    if (receive_buffer && receive_size > 0) memset(receive_buffer, 0, (size_t)receive_size);
    if (end_function) end_function(end_param);
    STUB("sceSifCallRpc (no IOP servers yet)");
    return 0;
}
int sceSifCheckStatRpc(sceSifClientData *cd) { (void)cd; return 0; }
unsigned int sceSifSetDma(sceSifDmaData *transfers, int count) { (void)transfers; (void)count; return 1; }
int sceSifDmaStat(unsigned int id) { (void)id; return -1; }

/* ---- libsdr --------------------------------------------------------------------------------- */
int sceSdRemoteInit(void) { return 0; }
int sceSdRemote(int wait, int command, ...) { (void)wait; (void)command; STUB("sceSdRemote (no sound yet)"); return 0; }

/* ---- libdev --------------------------------------------------------------------------------- */
int sceDevVu0Pause(void) { return 0; }
int sceDevVu0SetTBit(void) { return 0; }
int sceDevVu0IsRunning(void) { return 0; }
int sceDevVif0Reset(void) { return 0; }
int sceDevVif1Reset(void) { return 0; }
int sceDevVu0Reset(void) { return 0; }
int sceDevVu1Reset(void) { return 0; }
