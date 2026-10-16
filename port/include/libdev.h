/*
 * port/include/libdev.h
 *
 * The libdev calls the game makes at start-up to reset VU0 and the VIFs (core/system.c), for the PC
 * port (port/sdk/libdev.c), where there is nothing to reset. The libdev members the decomp has as
 * func_0022*.c are not built on the PC.
 */
#ifndef SCE_LIBDEV_H
#define SCE_LIBDEV_H

int sceDevVu0Pause(void);
int sceDevVu0SetTBit(void);
int sceDevVu0IsRunning(void);
int sceDevVif0Reset(void);
int sceDevVif1Reset(void);
int sceDevVu0Reset(void);
int sceDevVu1Reset(void);

#endif /* SCE_LIBDEV_H */
