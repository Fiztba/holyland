/*
 * port/include/libmpeg.h
 *
 * libmpeg for the PC port (port/sdk/libmpeg.c): until movies are decoded on the PC, every movie ends
 * at once.
 */
#ifndef SCE_LIBMPEG_LIBMPEG_H
#define SCE_LIBMPEG_LIBMPEG_H

#include <stdint.h>

typedef struct {
    int width, height, frameCount;
    int64_t pts, dts;
    int64_t flags;
    int64_t pts2nd, dts2nd;
    int64_t flags2nd;
    void *sys;
} sceMpeg;

typedef struct {
    int type;
    void *data;
    int len;
    int64_t pts, dts;
} sceMpegCbDataStr;

typedef int (*sceMpegCallback)(sceMpeg *mp, void *cbdata, void *anyData);

void sceMpegInit(void);
int sceMpegCreate(sceMpeg *mp, void *work_area, int size);
int sceMpegDelete(sceMpeg *mp);
void sceMpegReset(sceMpeg *mp);
sceMpegCallback sceMpegAddCallback(sceMpeg *mp, int type, sceMpegCallback func, void *data);
int sceMpegAddStrCallback(sceMpeg *mp, int type, int ch, sceMpegCallback func, void *data);
int sceMpegDemuxPssRing(sceMpeg *mp, void *buf, int size, void *ring, int ring_size);
int sceMpegGetPicture(sceMpeg *mp, void *rgb32, int mbcount);
int sceMpegIsEnd(sceMpeg *mp);
int sceMpegIsRefBuffEmpty(sceMpeg *mp);
void sceMpegSetDecodeMode(sceMpeg *mp, int ni, int np, int nb);

#endif /* SCE_LIBMPEG_LIBMPEG_H */
