/*
 * port/include/libcdvd.h
 *
 * libcdvd for the PC port (port/sdk/libcdvd.c): the disc, read from the user's extracted disc or
 * image later. Its records are the library's, the PC keeps them as they are.
 */
#ifndef SCE_LIBCDVD_LIBCDVD_H
#define SCE_LIBCDVD_LIBCDVD_H

typedef struct {
    unsigned char trycount;
    unsigned char spindlctrl;
    unsigned char datapattern;
    unsigned char pad;
} sceCdRMode;

typedef struct sceCdCLOCK {
    unsigned char stat;
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char pad;
    unsigned char day;
    unsigned char month;
    unsigned char year;
} sceCdCLOCK;

typedef struct sceCdlFILE {
    unsigned int lsn;
    unsigned int size;
    char name[16];
    unsigned char date[8];
} sceCdlFILE;

int sceCdInit(int mode);
int sceCdMmode(int media);
int sceCdDiskReady(int mode);
int sceCdSync(int mode);
int sceCdGetError(void);
int sceCdBreak(void);
int sceCdSearchFile(sceCdlFILE *file, const char *name);
int sceCdRead(unsigned int lsn, unsigned int sectors, void *buf, sceCdRMode *mode);
int sceCdSeek(unsigned int lsn);
int sceCdReadClock(sceCdCLOCK *clock);
/* Streaming (the movie player). */
int sceCdStInit(unsigned int bufmax, unsigned int bankmax, void *buffer);
int sceCdStStart(unsigned int lsn, sceCdRMode *mode);
int sceCdStRead(unsigned int sectors, void *buffer, unsigned int mode, unsigned int *error);
int sceCdStSeek(unsigned int lsn);
int sceCdStStop(void);

#endif /* SCE_LIBCDVD_LIBCDVD_H */
