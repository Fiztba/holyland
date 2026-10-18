/*
 * port/include/libmc.h
 *
 * libmc for the PC port (port/sdk/libmc.c): memory cards, kept as folders later. The directory
 * record is the card's own (it is filled by sceMcGetDir), so its layout stays.
 */
#ifndef SCE_LIBMC_LIBMC_H
#define SCE_LIBMC_LIBMC_H

typedef struct sceMcStDateTime {
    unsigned char Resv2;
    unsigned char Sec;
    unsigned char Min;
    unsigned char Hour;
    unsigned char Day;
    unsigned char Month;
    unsigned short Year;
} sceMcStDateTime;

typedef struct sceMcTblGetDir {
    sceMcStDateTime _Create;     /* 0x00 */
    sceMcStDateTime _Modify;     /* 0x08 */
    unsigned int FileSizeByte;   /* 0x10 */
    unsigned short AttrFile;     /* 0x14 */
    unsigned short Reserve1;     /* 0x16 */
    unsigned int Reserve2;       /* 0x18 */
    unsigned int PdaAplNo;       /* 0x1C */
    unsigned char EntryName[32]; /* 0x20 */
} sceMcTblGetDir;

int sceMcInit(void);
int sceMcGetInfo(int port, int slot, int *type, int *free, int *format);
int sceMcOpen(int port, int slot, const char *name, int mode);
int sceMcClose(int fd);
int sceMcRead(int fd, void *buffer, int size);
int sceMcWrite(int fd, const void *buffer, int size);
int sceMcSeek(int fd, int offset, int origin);
int sceMcMkdir(int port, int slot, const char *name);
int sceMcChdir(int port, int slot, const char *new_dir, char *old_dir);
int sceMcGetDir(int port, int slot, const char *name, unsigned int mode, int max_entries,
                sceMcTblGetDir *table);
int sceMcFormat(int port, int slot);
int sceMcDelete(int port, int slot, const char *name);
/* Waits for (mode 0) or polls (mode 1) the call in flight: returns 1 when one finished (its function
   number in *cmd and result in *result), 0 while it runs, -1 when none is in flight. */
int sceMcSync(int mode, int *cmd, int *result);

#endif /* SCE_LIBMC_LIBMC_H */
