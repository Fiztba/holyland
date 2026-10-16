/*
 * port/include/libpad.h
 *
 * libpad for the PC port (port/sdk/libpad.c): the controllers, read from SDL or a test script later.
 */
#ifndef SCE_LIBPAD_LIBPAD_H
#define SCE_LIBPAD_LIBPAD_H

/* scePadGetState's answers. */
#define scePadStateDiscon 0
#define scePadStateFindPad 1
#define scePadStateFindCTP1 2
#define scePadStateExecCmd 5
#define scePadStateStable 6
#define scePadStateError 7

int scePadInit(int mode);
int scePadPortOpen(int port, int slot, void *dma_buffer);
int scePadRead(int port, int slot, void *data);
int scePadGetState(int port, int slot);
int scePadGetReqState(int port, int slot);
int scePadInfoMode(int port, int slot, int term, int index);
int scePadSetMainMode(int port, int slot, int mode, int lock);
int scePadInfoAct(int port, int slot, int actno, int term);
int scePadSetActAlign(int port, int slot, const unsigned char *data);
int scePadSetActDirect(int port, int slot, const unsigned char *data);
int scePadInfoPressMode(int port, int slot);
int scePadEnterPressMode(int port, int slot);
int scePadExitPressMode(int port, int slot);

#endif /* SCE_LIBPAD_LIBPAD_H */
