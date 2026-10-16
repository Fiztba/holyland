/*
 * port/include/sifdev.h
 *
 * File calls through the IOP (cdrom0:, host0:) and the IOP's modules and heap, for the PC port
 * (port/sdk/sif.c). The IOP heap hands out IOP addresses, which the game keeps as numbers; the PC
 * hands out offsets in a host array, so they stay ints.
 */
#ifndef SCE_LIBKERNL_SIFDEV_H
#define SCE_LIBKERNL_SIFDEV_H

#define SCE_RDONLY 0x0001
#define SCE_WRONLY 0x0002
#define SCE_RDWR 0x0003
#define SCE_CREAT 0x0200
#define SCE_TRUNC 0x0400

#define SCE_SEEK_SET 0
#define SCE_SEEK_CUR 1
#define SCE_SEEK_END 2

int sceOpen(const char *name, int flags, ...);
int sceClose(int fd);
int sceRead(int fd, void *buffer, int size);
int sceWrite(int fd, const void *buffer, int size);
int sceLseek(int fd, int offset, int whence);
int sceFsReset(void);

int sceSifRebootIop(const char *image);
int sceSifSyncIop(void);
int sceSifLoadModule(const char *path, int arg_length, const char *args);
int sceSifInitIopHeap(void);
int sceSifAllocIopHeap(int size); /* an IOP address, 0 on failure */
int sceSifFreeIopHeap(int iop_address);
/* Loads a file into the IOP heap at iop_address; returns the size or a negative error. */
int sceSifLoadIopHeap(const char *path, int iop_address);

#endif /* SCE_LIBKERNL_SIFDEV_H */
