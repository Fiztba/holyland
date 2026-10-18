/*
 * port/include/libsdr.h
 *
 * The sound library's remote calls (sceSdRemote: libsd run on the IOP through RPC), for the PC port
 * (port/sdk/libsdr.c). Until sound runs on the PC they are accepted and do nothing.
 */
#ifndef SCE_LIBSDR_H
#define SCE_LIBSDR_H

int sceSdRemoteInit(void);
/* Calls libsd function `command` on the IOP with the arguments that follow; returns its result. */
int sceSdRemote(int wait, int command, ...);

#endif /* SCE_LIBSDR_H */
