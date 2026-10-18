/* The Sony libraries that are still stand-ins: libdev, libmpeg until movies, libsdr until sound
   (step 4). Each does nothing, or the least that keeps the game going, and logs once that it was
   reached. The rest of the SDK is in port/sdk: libcdvd.c, libdma.c, libgraph.c, libmc.c, libpad.c
   and sif.c, and the kernel in port/platform/kernel.c. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eeregs.h"
#include "libdev.h"
#include "libgraph.h"
#include "libmpeg.h"
#include "libsdr.h"
#include "port/host.h"
#include "port/platform.h"

#define STUB(name) PORT_TODO(name " (stub)")

/* ---- libmpeg: every movie is over at once --------------------------------------------------- */
void sceMpegInit(void) {}
int sceMpegCreate(sceMpeg *mp, void *work_area, int size) { (void)work_area; (void)size; memset(mp, 0, sizeof(*mp)); port_event("movie: none played yet, so it is over at once"); return 1; }
int sceMpegDelete(sceMpeg *mp) { (void)mp; return 0; }
void sceMpegReset(sceMpeg *mp) { (void)mp; }
sceMpegCallback sceMpegAddCallback(sceMpeg *mp, int type, sceMpegCallback func, void *data) { (void)mp; (void)type; (void)data; return func; }
int sceMpegAddStrCallback(sceMpeg *mp, int type, int ch, sceMpegCallback func, void *data) { (void)mp; (void)type; (void)ch; (void)func; (void)data; return 0; }
int sceMpegDemuxPssRing(sceMpeg *mp, void *buf, int size, void *ring, int ring_size) { (void)mp; (void)buf; (void)ring; (void)ring_size; return size; }
int sceMpegGetPicture(sceMpeg *mp, void *rgb32, int mbcount) { (void)mp; (void)rgb32; (void)mbcount; return -1; }
int sceMpegIsEnd(sceMpeg *mp) { (void)mp; return 1; }
int sceMpegIsRefBuffEmpty(sceMpeg *mp) { (void)mp; return 1; }
void sceMpegSetDecodeMode(sceMpeg *mp, int ni, int np, int nb) { (void)mp; (void)ni; (void)np; (void)nb; }

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
