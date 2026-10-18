/*
 * port/include/sifrpc.h
 *
 * SIF remote procedure calls and DMA to the IOP, for the PC port (port/sdk/sif.c). The servers the
 * game binds (the sound driver's) run on the host. The client record is the PC's own: its layout is
 * free, so the game must keep it as this type, not as an int array.
 */
#ifndef SCE_LIBKERNL_SIFRPC_H
#define SCE_LIBKERNL_SIFRPC_H

/* A transfer to the IOP: src is EE memory, dest an IOP address (a number). */
typedef struct sceSifDmaData {
    void *data;
    unsigned int addr;
    int size;
    int mode;
} sceSifDmaData;

typedef void (*sceSifEndFunc)(void *param);

typedef struct sceSifClientData {
    void *server;        /* the host server the bind found; NULL until bound */
    unsigned int command; /* the server id */
    int busy;            /* a call is in flight */
    void *buffer;        /* the server's receive buffer */
} sceSifClientData;
typedef sceSifClientData sceSifRpcClientData;

#define SIF_RPCM_NOWAIT 0x01

void sceSifInitRpc(int mode);
/* Binds cd to the server with id server_id; cd->server stays NULL until the server exists. */
int sceSifBindRpc(sceSifClientData *cd, unsigned int server_id, int mode);
int sceSifCallRpc(sceSifClientData *cd, unsigned int function, unsigned int mode, void *send_buffer,
                  int send_size, void *receive_buffer, int receive_size, sceSifEndFunc end_function,
                  void *end_param);
int sceSifCheckStatRpc(sceSifClientData *cd);
/* Starts the transfers; returns an id for sceSifDmaStat, which is negative once they are done. */
unsigned int sceSifSetDma(sceSifDmaData *transfers, int count);
int sceSifDmaStat(unsigned int id);

#endif /* SCE_LIBKERNL_SIFRPC_H */
