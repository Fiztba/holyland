/*
 * port/include/eekernel.h
 *
 * The EE kernel calls the game makes, for the PC port (port/platform/kernel.c). The records are the
 * decomp's sce/libkernl/eekernel.h ones with host pointer fields; the PC never hands them to a real
 * kernel, so their layout is free.
 */
#ifndef SCE_LIBKERNL_EEKERNEL_H
#define SCE_LIBKERNL_EEKERNEL_H

#include <stdint.h>

struct SemaParam {
    int currentCount;
    int maxCount;
    int initCount;
    int numWaitThreads;
    unsigned int attr;
    unsigned int option;
};

struct ThreadParam {
    int status;
    void (*entry)(void *);
    void *stack;
    int stackSize;
    void *gpReg;
    int initPriority;
    int currentPriority;
    unsigned int attr;
    unsigned int option;
    int waitType;
    int waitId;
    int wakeupCount;
};

/* Interrupts. The causes the game uses: INTC_VBLANK_S 2, INTC_VBLANK_E 3, INTC_TIM0 9. */
int AddIntcHandler(int cause, int (*handler)(int cause), int next);
int RemoveIntcHandler(int cause, int id);
int AddDmacHandler(int channel, int (*handler)(int channel), int next);
int RemoveDmacHandler(int channel, int id);
int EnableIntc(int cause);
int DisableIntc(int cause);
int EnableDmac(int channel);
int DisableDmac(int channel);
int iEnableIntc(int cause);
int iDisableIntc(int cause);
int iEnableDmac(int channel);
int iDisableDmac(int channel);
int DIntr(void);
int EIntr(void);

/* Threads and semaphores. Windows' kernel32 exports a CreateThread, ExitThread and TerminateThread of
   its own, so the EE's are linked under other names (on every platform, to keep them the same). Don't
   include <windows.h> after this header. */
#define CreateThread ee_CreateThread
#define ExitThread ee_ExitThread
#define TerminateThread ee_TerminateThread
int CreateThread(struct ThreadParam *param);
int DeleteThread(int id);
int StartThread(int id, void *arg);
void ExitThread(void);
int TerminateThread(int id);
int ChangeThreadPriority(int id, int priority);
int RotateThreadReadyQueue(int priority);
int GetThreadId(void);
int SleepThread(void);
int WakeupThread(int id);
int iWakeupThread(int id);
int CreateSema(struct SemaParam *param);
int DeleteSema(int sema);
int SignalSema(int sema);
int iSignalSema(int sema);
int WaitSema(int sema);
int PollSema(int sema);

/* Stops the program (the EE's Exit). */
void Exit(int status);

/* Caches: nothing to do on the PC. */
void FlushCache(int operation);
void SyncDCache(void *start, void *end);
void iSyncDCache(void *start, void *end);
void InvalidDCache(void *start, void *end);

#endif /* SCE_LIBKERNL_EEKERNEL_H */
