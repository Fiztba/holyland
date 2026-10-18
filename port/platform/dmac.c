/* The EE's DMA controller, played by a host thread (port/include/port/platform.h).

   The game starts a channel by storing STR into its CHCR, in about fifty places, and then waits in a
   loop for the hardware to clear it again, often with no call to the host in between (the menus draw
   each glyph that way). Nothing on the PC sees a store into the register page, so this thread looks
   at the ten CHCRs, finishes each channel it finds started and clears its STR, as the hardware does
   alongside the CPU.

   Channels 1 (VIF1) and 2 (GIF) carry the graphics: their packets, in normal mode or as a source
   chain of DMA tags, are read here before STR is cleared, so the game cannot reuse a packet the GS
   has not taken. Channel 1 feeds VIF1 (port/platform/vif1.c), which runs VU1 and hands GIF packets
   on; channel 2 is the GIF's PATH3. Both end at the GS (port/platform/gs.c), which this thread alone
   talks to: it also scans out the vblanks the kernel queues, and runs what port_dmac_call asks for.
   The other channels finish at once: the disc, sound and movies are done by their libraries.

   The game's EE time does not move while it spins on a register (port/platform/kernel.c), so how soon
   this thread clears STR never changes what the game does, only how long the host takes.

   The thread spins while the game is starting channels, and naps once it has been idle for a while.
   The calls that come before a game's own channel start (FlushCache, the libgraph and libdma calls)
   wake it, so the nap rarely costs the game a wait. */
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#include <stdint.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"

#define CHCR_DIR 0x1 /* from memory */
#define CHCR_MOD_SHIFT 2
#define CHCR_ASP_SHIFT 4
#define CHCR_TTE 0x40
#define CHCR_TIE 0x80
#define CHCR_STR 0x100
/* a channel's registers, from its CHCR */
#define D_MADR 0x10
#define D_QWC 0x20
#define D_TADR 0x30
#define D_ASR0 0x40
#define D_ASR1 0x50
/* the source chain's tag IDs */
#define TAG_REFE 0
#define TAG_CNT 1
#define TAG_NEXT 2
#define TAG_REF 3
#define TAG_REFS 4
#define TAG_CALL 5
#define TAG_RET 6
#define TAG_END 7
/* polls with nothing started before the thread naps, and the nap */
#define SPINS_BEFORE_NAP 200000
#define NAP_SECONDS 0.0002

static const uint32_t chcr_address[10] = {
    0x10008000, /* 0 VIF0 */
    0x10009000, /* 1 VIF1 */
    0x1000A000, /* 2 GIF */
    0x1000B000, /* 3 from IPU */
    0x1000B400, /* 4 to IPU */
    0x1000C000, /* 5 SIF0 */
    0x1000C400, /* 6 SIF1 */
    0x1000C800, /* 7 SIF2 */
    0x1000D000, /* 8 from scratchpad */
    0x1000D400, /* 9 to scratchpad */
};

static volatile int *chcr[10];
static volatile uint64_t transfers[10];
static volatile int wake_requested;

static void (*volatile call_fn)(void *);
static void *call_arg;
static volatile int call_done;

#ifdef _WIN32
static CRITICAL_SECTION nap_lock;
static CONDITION_VARIABLE nap_signal;
#else
static pthread_mutex_t nap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t nap_signal = PTHREAD_COND_INITIALIZER;
#endif

static void cpu_relax(void) {
#if defined(__x86_64__) || defined(_M_X64)
    __builtin_ia32_pause();
#endif
}

static void nap(void) {
#ifdef _WIN32
    EnterCriticalSection(&nap_lock);
    if (!__atomic_load_n(&wake_requested, __ATOMIC_ACQUIRE)) {
        SleepConditionVariableCS(&nap_signal, &nap_lock, 1);
    }
    LeaveCriticalSection(&nap_lock);
#else
    struct timespec until;

    pthread_mutex_lock(&nap_lock);
    if (!__atomic_load_n(&wake_requested, __ATOMIC_ACQUIRE)) {
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += (long)(NAP_SECONDS * 1e9);
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&nap_signal, &nap_lock, &until);
    }
    pthread_mutex_unlock(&nap_lock);
#endif
}

/* ---- Reading a channel's packets --------------------------------------------------------------- */
#define CHANNEL_REG(channel, offset) EE_HW(volatile uint32_t *, chcr_address[channel] + (offset))
/* more tags than any frame has: a chain that loops */
#define MAX_TAGS 4000000

/* What a MADR, TADR or tag address names. Bit 31 is the scratchpad (its low 14 bits); the game
   writes either the scratchpad's offset or its EE address, 0x70000000, under that bit. Otherwise it
   is an EE address in game RAM or the program (port/platform/ee_address.c). */
static const uint8_t *dma_ptr(uint32_t address) {
    if (address & 0x80000000u) {
        uint32_t rest = address & 0x7FFFFFFFu;

        if (rest < sizeof(ee_scratchpad) || rest - EE_SCRATCHPAD_ADDRESS < sizeof(ee_scratchpad)) {
            return ee_scratchpad + (rest & (sizeof(ee_scratchpad) - 16));
        }
        return ee_ptr_of(rest);
    }
    if (address < EE_RAM_SIZE) {
        return ee_ram + address;
    }
    return ee_ptr_of(address);
}

/* The memory side of a transfer of qwords. */
static void channel_send(int channel, uint32_t address, uint32_t qwords) {
    const uint8_t *data;

    if (qwords == 0) {
        return;
    }
    data = dma_ptr(address);
    if (channel == 1) {
        port_vif1_feed((const uint32_t *)data, (size_t)qwords * 4);
    } else {
        port_gs_gif(3, data, (size_t)qwords * 16);
    }
}

/* Channel 1 or 2 in normal mode: QWC qwords from MADR (to memory, for VIF1: the GS's FIFO). */
static void run_normal(int channel, int value) {
    uint32_t madr = *CHANNEL_REG(channel, D_MADR);
    uint32_t qwc = *CHANNEL_REG(channel, D_QWC) & 0xFFFF;

    if (channel == 1 && !(value & CHCR_DIR)) {
        port_gs_read_fifo((void *)dma_ptr(madr), qwc);
    } else {
        /* the GIF's channel only goes to the GS, whatever DIR says (ui/title.c leaves it 0) */
        channel_send(channel, madr, qwc);
    }
    *CHANNEL_REG(channel, D_MADR) = madr + qwc * 16;
    *CHANNEL_REG(channel, D_QWC) = 0;
}

/* Channel 1 or 2 in source chain mode; returns CHCR's TAG field, the last tag's upper 16 bits. */
static uint32_t run_chain(int channel, int value) {
    uint32_t tadr = *CHANNEL_REG(channel, D_TADR);
    uint32_t madr = *CHANNEL_REG(channel, D_MADR);
    uint32_t qwc = *CHANNEL_REG(channel, D_QWC) & 0xFFFF;
    uint32_t asr[2];
    int asp = (value >> CHCR_ASP_SHIFT) & 3;
    uint32_t tag_bits = (uint32_t)value >> 16;
    int tags;

    asr[0] = *CHANNEL_REG(channel, D_ASR0);
    asr[1] = *CHANNEL_REG(channel, D_ASR1);

    /* started with QWC set: those qwords at MADR go first, and the tag CHCR holds (the one they came
       from) says whether the chain goes on. The game starts some chains that way with TAG 0, a
       REFE: just the qwords. */
    if (qwc != 0) {
        uint32_t id = (tag_bits >> 12) & 7;

        channel_send(channel, madr, qwc);
        madr += qwc * 16;
        qwc = 0;
        if (id == TAG_REFE || id == TAG_END || ((tag_bits & 0x8000) && (value & CHCR_TIE))) {
            goto done;
        }
    }

    for (tags = 0; tags < MAX_TAGS; tags++) {
        const uint32_t *tag = (const uint32_t *)dma_ptr(tadr);
        uint32_t tag_qwc = tag[0] & 0xFFFF;
        uint32_t id = (tag[0] >> 28) & 7;
        int irq = (int)(tag[0] >> 31);
        uint32_t address = tag[1] & 0x8FFFFFF0u;
        int end = 0;

        if (channel == 1 && (value & CHCR_TTE)) {
            port_vif1_feed(tag + 2, 2); /* the tag's upper half is VIF codes */
        }
        tag_bits = tag[0] >> 16;
        switch (id) {
        case TAG_REFE:
            channel_send(channel, address, tag_qwc);
            madr = address + tag_qwc * 16;
            tadr += 16;
            end = 1;
            break;
        case TAG_CNT:
            channel_send(channel, tadr + 16, tag_qwc);
            madr = tadr + 16 + tag_qwc * 16;
            tadr = madr;
            break;
        case TAG_NEXT:
            channel_send(channel, tadr + 16, tag_qwc);
            madr = tadr + 16 + tag_qwc * 16;
            tadr = address;
            break;
        case TAG_REF:
        case TAG_REFS:
            channel_send(channel, address, tag_qwc);
            madr = address + tag_qwc * 16;
            tadr += 16;
            break;
        case TAG_CALL:
            channel_send(channel, tadr + 16, tag_qwc);
            madr = tadr + 16 + tag_qwc * 16;
            if (asp >= 2) {
                PORT_FATAL("DMA channel %d: a CALL tag three deep", channel);
            }
            asr[asp++] = madr;
            tadr = address;
            break;
        case TAG_RET:
            channel_send(channel, tadr + 16, tag_qwc);
            madr = tadr + 16 + tag_qwc * 16;
            if (asp > 0) {
                tadr = asr[--asp];
            } else {
                end = 1;
            }
            break;
        case TAG_END:
            channel_send(channel, tadr + 16, tag_qwc);
            madr = tadr + 16 + tag_qwc * 16;
            end = 1;
            break;
        }
        if (end || (irq && (value & CHCR_TIE))) {
            goto done;
        }
    }
    PORT_FATAL("DMA channel %d: a chain of more than %d tags (it loops?)", channel, MAX_TAGS);

done:
    *CHANNEL_REG(channel, D_TADR) = tadr;
    *CHANNEL_REG(channel, D_MADR) = madr;
    *CHANNEL_REG(channel, D_QWC) = 0;
    *CHANNEL_REG(channel, D_ASR0) = asr[0];
    *CHANNEL_REG(channel, D_ASR1) = asr[1];
    return tag_bits;
}

/* Finishes the channels that are started; 1 when there was one. */
static int run_channels(void) {
    int found = 0;
    int channel;

    for (channel = 0; channel < 10; channel++) {
        int value = __atomic_load_n(chcr[channel], __ATOMIC_ACQUIRE);

        if (value & CHCR_STR) {
            /* a vblank queued now came before this start (port_gs_post_vsync) */
            while ((channel == 1 || channel == 2) && port_gs_vsync_pending()) {
                port_gs_run_vsync();
            }
            if ((channel == 1 || channel == 2) && ((value >> CHCR_MOD_SHIFT) & 3) == 1) {
                uint32_t tag = run_chain(channel, value);

                /* STR and ASP clear, and TAG the last tag's upper half */
                __atomic_store_n(chcr[channel], (int)((value & 0xFFCF & ~CHCR_STR) | (tag << 16)), __ATOMIC_RELEASE);
            } else {
                if (channel == 1 || channel == 2) {
                    run_normal(channel, value);
                }
                __atomic_fetch_and(chcr[channel], ~CHCR_STR, __ATOMIC_RELEASE);
            }
            __atomic_fetch_add(&transfers[channel], 1, __ATOMIC_RELAXED);
            port_kernel_post_dmac(channel);
            found = 1;
        }
    }
    return found;
}

#ifdef _WIN32
static DWORD WINAPI dmac_thread(void *unused)
#else
static void *dmac_thread(void *unused)
#endif
{
    int idle = 0;

    (void)unused;
    for (;;) {
        if (run_channels()) {
            idle = 0;
            continue;
        }
        if (__atomic_load_n(&call_fn, __ATOMIC_ACQUIRE) != NULL) {
            call_fn(call_arg);
            __atomic_store_n(&call_fn, NULL, __ATOMIC_RELAXED);
            __atomic_store_n(&call_done, 1, __ATOMIC_RELEASE);
            idle = 0;
            continue;
        }
        if (port_gs_vsync_pending()) {
            port_gs_run_vsync();
            idle = 0;
            continue;
        }
        if (__atomic_exchange_n(&wake_requested, 0, __ATOMIC_ACQ_REL)) {
            idle = 0;
        }
        if (++idle < SPINS_BEFORE_NAP) {
            cpu_relax();
            continue;
        }
        nap();
    }
    return 0;
}

void port_dmac_start(void) {
    int channel;

    for (channel = 0; channel < 10; channel++) {
        chcr[channel] = EE_HW(volatile int *, chcr_address[channel]);
    }
    port_vif1_reset();
    port_vu1_reset();
#ifdef _WIN32
    InitializeCriticalSection(&nap_lock);
    InitializeConditionVariable(&nap_signal);
    if (CreateThread(NULL, 0, dmac_thread, NULL, 0, NULL) == NULL) {
        PORT_FATAL("cannot start the DMA controller's thread");
    }
#else
    {
        pthread_t thread;

        if (pthread_create(&thread, NULL, dmac_thread, NULL) != 0) {
            PORT_FATAL("cannot start the DMA controller's thread");
        }
        pthread_detach(thread);
    }
#endif
}

void port_dmac_wake(void) {
    if (__atomic_exchange_n(&wake_requested, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
#ifdef _WIN32
    EnterCriticalSection(&nap_lock);
    WakeConditionVariable(&nap_signal);
    LeaveCriticalSection(&nap_lock);
#else
    pthread_mutex_lock(&nap_lock);
    pthread_cond_signal(&nap_signal);
    pthread_mutex_unlock(&nap_lock);
#endif
}

void port_dmac_wait(int channel) {
    port_dmac_wake();
    while (__atomic_load_n(chcr[channel], __ATOMIC_ACQUIRE) & CHCR_STR) {
        cpu_relax();
    }
}

volatile int *port_dmac_chcr(int channel) {
    return chcr[channel];
}

uint64_t port_dmac_transfers(int channel) {
    return __atomic_load_n(&transfers[channel], __ATOMIC_RELAXED);
}

void port_dmac_call(void (*fn)(void *), void *arg) {
    call_arg = arg;
    __atomic_store_n(&call_done, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&call_fn, fn, __ATOMIC_RELEASE);
    port_dmac_wake();
    while (!__atomic_load_n(&call_done, __ATOMIC_ACQUIRE)) {
        cpu_relax();
    }
}
