/* MODHSYN, Sony's CSL synthesizer on the IOP ("SPU2_Synthesizer_Module", PsIImodhsyn 2240), on the PC.

   Written from the disc's MODHSYN.IRX's code; the addresses in the comments are its functions' and
   data's, with the module loaded at 0x100000. KF4's sound driver (fsiopsnd.c) gives it sixteen
   ports, each reading a stream of MIDI messages (from a sequence port or from the game's sound
   effects) and playing them with the port's .hd banks: Prog (programs) -> Split (key ranges) ->
   Sset (velocity ranges) -> Smpl (samples, envelopes, LFOs) -> Vagi (the sample in sound memory).

   The 48 SPU2 voices are shared by all ports. A note-on takes a free voice (or steals one) and
   queues it; each tick keys the queued voices on once their old sound has died away (ENVX < 1000),
   then polls the voices that play (ENVX, ENDX) to free the finished ones, and steps their LFOs,
   portamento, pitch and volume. Every SPU2 access is a libsd call (sd.h), made in the order
   MODHSYN makes it, reads included: they decide which voices die and when.

   The PS2's integer arithmetic is kept: 32-bit products wrap, divisions truncate, and a few
   shifts round toward zero (the compiler's "add 2^n - 1 if negative" idiom). Where the IOP would
   trap on a division by zero, this logs once and carries on.

   Not done, as KF4 never uses them: sound-effect message ports (portMode 1) and Setb banks
   (waveType 1); and sceHSyn_SetReservVoice, which KF4 does not import (its mask stays 0, but the
   tick's reads of reserved voices are kept). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "csl.h"
#include "port/platform.h"
#include "sd.h"

/* libsd selectors: a voice's parameter is core | voice << 1 | parameter */
#define VP_VOLL 0x0000
#define VP_VOLR 0x0100
#define VP_PITCH 0x0200
#define VP_ADSR1 0x0300
#define VP_ADSR2 0x0400
#define VP_ENVX 0x0500
#define SW_NON 0x1400
#define SW_KON 0x1500
#define SW_KOFF 0x1600
#define SW_ENDX 0x1700
#define SW_VMIXL 0x1800
#define SW_VMIXEL 0x1900
#define SW_VMIXR 0x1A00
#define SW_VMIXER 0x1B00
#define VA_SSA 0x2040
#define CA_EFFECT_ENABLE 0x02
#define CA_NOISE_CLK 0x08

/* The release written to a voice cut at once: ADSR2 with the fastest exponential release */
#define ADSR2_FAST_RELEASE 0xC021

#define LOG_ONCE(...)                \
    do {                             \
        static int logged_;          \
        if (!logged_) {              \
            logged_ = 1;             \
            port_event(__VA_ARGS__); \
        }                            \
    } while (0)

/* ---- Reading the .hd (little-endian, unaligned) -------------------------------------------------- */
static uint32_t rd32(const uint8_t *p, uint32_t offset) {
    uint32_t v;

    memcpy(&v, p + offset, 4);
    return v;
}

static uint16_t rd16(const uint8_t *p, uint32_t offset) {
    uint16_t v;

    memcpy(&v, p + offset, 2);
    return v;
}

static uint8_t u8at(const uint8_t *p, uint32_t offset) {
    return p[offset];
}

static int32_t s8at(const uint8_t *p, uint32_t offset) {
    return (int8_t)p[offset];
}

static int32_t s16at(const uint8_t *p, uint32_t offset) {
    return (int16_t)rd16(p, offset);
}

/* Entry idx of a chunk (+0xC the highest index, +0x10 the entries' offsets from the chunk), NULL
   past the end. An offset of -1 is not checked: the entry is then the byte before the chunk. */
static const uint8_t *chunk_entry(const uint8_t *chunk, uint32_t idx) {
    if (rd32(chunk, 0xC) < idx) {
        return NULL;
    }
    return chunk + (int32_t)rd32(chunk, 0x10 + idx * 4);
}

/* ---- Integer helpers ----------------------------------------------------------------------------- */
/* x >> n rounded toward zero */
static int32_t shr_trunc(int32_t x, int n) {
    return (x < 0 ? x + ((1 << n) - 1) : x) >> n;
}

/* The R3000's mult: the low 32 bits of the product */
static int32_t mul32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

static int32_t clamp16(int32_t x) {
    return x > 0x7FFF ? 0x7FFF : x < -0x8000 ? -0x8000 : x;
}

static int32_t clamp8(int32_t x) {
    return x >= 0x80 ? 0x7F : x < -0x80 ? -0x80 : x;
}

/* ---- The voices ---------------------------------------------------------------------------------- */
enum { WAVE_NONE, WAVE_SAW_UP, WAVE_SAW_DOWN, WAVE_TRIANGLE, WAVE_SQUARE, WAVE_RANDOM, WAVE_TABLE };

/* An LFO (0x28 bytes at voice+0x78 for the pitch, +0xA0 for the amplitude) */
typedef struct {
    uint8_t state;        /* 0 off, 1 delay, 2 fade in, 3 on, 4 delay after key-off, 5 fade out, 6 done */
    uint8_t keyoffEnable; /* key-off starts or stops it */
    uint16_t delay;       /* ticks */
    uint16_t counter;
    int32_t fade;         /* 16.16 */
    uint32_t fadeStep;
    int16_t out;
    uint8_t wave;         /* WAVE_* (the PS2 keeps a function pointer) */
    int32_t phase;        /* 0..0xFFFF */
    uint32_t step;
    const int16_t *table;
    uint16_t tableLen;
} Lfo;

/* sceHSynEnv.lfoWaveTbl's entries */
typedef struct {
    uint8_t id;
    uint8_t pad;
    uint16_t len;
    const int16_t *data;
} LfoWave;

/* voice flags (+0x10) */
#define VF_ONESHOT 0x001
#define VF_PORTA_UP 0x002   /* portamento offset < 0, rising to 0 */
#define VF_PORTA_DOWN 0x004
#define VF_PENDING 0x008    /* waiting to key on */
#define VF_DAMPER 0x100     /* released while the damper was down */
#define VF_HELD 0x200       /* key held */

/* A voice (0x128 bytes at 0x10B500) */
typedef struct {
    HSynLink coreLink;      /* its core's free, pending or active list */
    HSynLink chLink;        /* its channel's lists */
    uint16_t flags;
    uint16_t envx;          /* ENVX as last polled at a note-on (the stealing's tie-break) */
    uint16_t priority;
    uint16_t volBase;       /* program x split x sample x velocity x crossfade */
    uint16_t volume;        /* volBase x channel volume x expression */
    uint16_t lastL;         /* the VOLL/VOLR written last (0xFFFF: write them) */
    uint16_t lastR;
    int16_t panSum;         /* the program's, split's and sample's pans */
    int16_t plfoDepth[2];   /* pitch LFO depth for positive and negative output, with CC1 */
    int16_t plfoBase[2];    /* the same without CC1 */
    uint16_t exBend;        /* F9 pitch bend (bit 15: none) */
    uint16_t portaRate;     /* portamento step per tick */
    uint16_t pitchOverride; /* sound-effect voices only */
    int32_t pitchCache;     /* the pitch offset written last (0x7FFFFFFF: write it) */
    int32_t bendOffset;     /* 1/128 semitones */
    int32_t portaOffset;
    uint32_t rateRatio;     /* the sample rate / 48000, 16.16 */
    uint8_t core;
    uint8_t voice;
    uint8_t key;
    uint32_t id;            /* 0xFF for MIDI note-ons */
    uint8_t velocity;
    uint8_t group;          /* exclusive group */
    uint8_t velCurve;
    uint8_t grace;          /* ticks after key-on in which it is not freed */
    uint8_t panL;           /* 0..128 */
    uint8_t panR;
    uint8_t surround;
    int8_t alfoDepth[2];
    int8_t alfoBase[2];
    uint8_t phase;          /* 0: volumes written inverted */
    uint8_t expression;
    uint8_t exExpression;   /* F9 expression (0xFF: none) */
    uint8_t exPan;          /* F9 pan (bit 7: none) */
    uint8_t owner;          /* 0 MIDI, 1 sound effect, 0xFF free */
    uint8_t waveType;
    int8_t panPos;
    HSynEnv *env;           /* the port */
    HSynChannel *ch;
    const uint8_t *prog;
    const uint8_t *split;
    const uint8_t *smpl;
    const uint8_t *vagi;    /* NULL: a noise voice */
    uint32_t bdSpu;
    Lfo plfo;
    Lfo alfo;
} Voice;

static Voice voices[2][24];
static HSynLink freeList[2];       /* 0x10B470 */
static HSynLink reservedList[2];   /* 0x10B4E0 */
static HSynLink pendingList[2];    /* 0x10EC80 */
static HSynLink activeList[2];     /* 0x10B4B0 */
static uint8_t freeCount[2];       /* 0x10EC98 */
static uint32_t reserveMask[2];    /* 0x10B4A0 */
static uint32_t nonMask[2];        /* 0x10B488 */
static uint32_t konMask[2];        /* 0x10B490 */
static uint32_t koffMask[2];       /* 0x10B4C8 */
static uint32_t vmixl[2];          /* 0x10B4D0 */
static uint32_t vmixr[2];          /* 0x10B4D4 */
static uint32_t vmixel[2];         /* 0x10ECA0 */
static uint32_t vmixer[2];         /* 0x10ECA4 */
static uint32_t tickUnit;          /* 0x10B480: the tick in ms, 8.8 */
static HSynVoiceStat *voiceStat;   /* 0x10B4C0 */
static uint32_t pollPending = 1;   /* 0x10B4C4: the voices have not been polled this tick */
static uint8_t mono;               /* 0x10ECB0 */
static uint8_t midiVoices;         /* 0x10ECB1 */
static uint8_t seVoices;           /* 0x10EC90 */
static uint32_t randState = 0x02CEE9D7u; /* 0x10B410, set when the module loads */

/* 0x10B280: one period of a sine, LFO wave 6: trunc(32767 * sin(2 * pi * i / 200)), every entry */
static const int16_t sine200[200] = {
    0, 1029, 2057, 3083, 4106, 5125, 6139, 7147, 8148, 9141,
    10125, 11099, 12062, 13013, 13951, 14875, 15785, 16679, 17557, 18417,
    19259, 20083, 20886, 21669, 22430, 23169, 23886, 24578, 25247, 25891,
    26509, 27100, 27666, 28203, 28713, 29195, 29648, 30072, 30465, 30829,
    31163, 31465, 31737, 31977, 32186, 32363, 32508, 32621, 32702, 32750,
    32767, 32750, 32702, 32621, 32508, 32363, 32186, 31977, 31737, 31465,
    31163, 30829, 30465, 30072, 29648, 29195, 28713, 28203, 27666, 27100,
    26509, 25891, 25247, 24578, 23886, 23169, 22430, 21669, 20886, 20083,
    19259, 18417, 17557, 16679, 15785, 14875, 13951, 13013, 12062, 11099,
    10125, 9141, 8148, 7147, 6139, 5125, 4106, 3083, 2057, 1029,
    0, -1029, -2057, -3083, -4106, -5125, -6139, -7147, -8148, -9141,
    -10125, -11099, -12062, -13013, -13951, -14875, -15785, -16679, -17557, -18417,
    -19259, -20083, -20886, -21669, -22430, -23169, -23886, -24578, -25247, -25891,
    -26509, -27100, -27666, -28203, -28713, -29195, -29648, -30072, -30465, -30829,
    -31163, -31465, -31737, -31977, -32186, -32363, -32508, -32621, -32702, -32750,
    -32767, -32750, -32702, -32621, -32508, -32363, -32186, -31977, -31737, -31465,
    -31163, -30829, -30465, -30072, -29648, -29195, -28713, -28203, -27666, -27100,
    -26509, -25891, -25247, -24578, -23886, -23169, -22430, -21669, -20886, -20083,
    -19259, -18417, -17557, -16679, -15785, -14875, -13951, -13013, -12062, -11099,
    -10125, -9141, -8148, -7147, -6139, -5125, -4106, -3083, -2057, -1029,
};

#define VOICE_OF_CORE(l) ((Voice *)(void *)((char *)(l) - offsetof(Voice, coreLink)))
#define VOICE_OF_CH(l) ((Voice *)(void *)((char *)(l) - offsetof(Voice, chLink)))

static void list_init(HSynLink *head) {
    head->next = head;
    head->prev = head;
}

/* 0x10345C / 0x1034B4 */
static void link_front(HSynLink *node, HSynLink *head) {
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}

/* 0x103478 */
static void link_tail(HSynLink *node, HSynLink *head) {
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

/* 0x103494 / 0x1034F4 */
static void unlink_node(HSynLink *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static uint16_t voice_id(const Voice *v) {
    return (uint16_t)(v->core | v->voice << 1);
}

static void stat_state(const Voice *v, uint8_t keep, uint8_t set) {
    if (voiceStat != NULL) {
        uint8_t *s = &voiceStat->voice_state[v->core][v->voice];

        *s = (uint8_t)((*s & keep) | set);
    }
}

/* 0x109720 */
static int32_t hsyn_rand(void) {
    randState = randState * 0x41C64E6Du + 0x3039u;
    return (int32_t)randState >> 16;
}

/* ---- Freeing and allocation ---------------------------------------------------------------------- */
static void release_counts(Voice *v) {
    if (v->env->voices != 0) {
        v->env->voices--;
    }
    if (v->owner == 0) {
        if (midiVoices != 0) {
            midiVoices--;
        }
    } else if (v->owner == 1) {
        if (seVoices != 0) {
            seVoices--;
        }
    }
}

/* 0x105B00: back to the free list (already off its channel's lists) */
static void voice_free_unlinked(Voice *v) {
    v->flags = 0;
    release_counts(v);
    unlink_node(&v->coreLink);
    freeCount[v->core]++;
    link_front(&v->coreLink, &freeList[v->core]);
    stat_state(v, 0x8F, 0);
    v->id = 0xFF;
    v->owner = 0xFF;
}

/* 0x105F94 */
static void voice_free(Voice *v) {
    unlink_node(&v->chLink);
    voice_free_unlinked(v);
}

/* Cut at once: key off with the fastest release, and free */
static void voice_kill(Voice *v) {
    koffMask[v->core] |= 1u << v->voice;
    sd_set_param((uint16_t)(voice_id(v) | VP_ADSR2), ADSR2_FAST_RELEASE);
    voice_free(v);
}

/* 0x1067C0: from the core the sample asks for (attr bits 4-5), else from the core with more free
   voices; the least recently freed voice */
static Voice *voice_alloc(uint8_t attr) {
    int core;
    HSynLink *l;

    if ((attr & 0x30) == 0x10) {
        core = 0;
    } else if ((attr & 0x30) == 0x20) {
        core = 1;
    } else {
        core = freeCount[0] < freeCount[1];
    }
    if (freeCount[core] == 0) {
        return NULL;
    }
    freeCount[core]--;
    l = freeList[core].prev;
    unlink_node(l);
    return VOICE_OF_CORE(l);
}

/* Whether a polled voice has finished: a held voice once its non-looping sample has ended
   (and not in its first ticks), a released one once its envelope is 0. *env is what the status
   buffer gets. */
static int voice_finished(Voice *v, uint32_t endx, uint16_t *env) {
    int ended = (endx >> v->voice & 1) != 0;

    if (v->flags & (VF_HELD | VF_DAMPER)) {
        if (v->grace != 0) {
            v->grace = ended ? (uint8_t)(v->grace - 1) : 0;
            *env = 0x7FFF;
            return 0;
        }
        return ended && *env == 0;
    }
    return *env == 0;
}

/* 0x104D20: a note-on that found no free voice polls the playing voices (once a tick) and frees
   the finished ones; 1 if it freed any */
static int poll_voices(void) {
    int freed = 0;
    int c;

    for (c = 0; c < 2; c++) {
        uint32_t endx = sd_get_switch((uint16_t)(SW_ENDX | c));
        HSynLink *l;

        for (l = activeList[c].prev; l != &activeList[c]; l = l->prev) {
            Voice *v = VOICE_OF_CORE(l);
            uint16_t env = sd_get_param((uint16_t)(voice_id(v) | VP_ENVX));

            int finished = voice_finished(v, endx, &env);

            v->envx = env;
            if (finished) {
                HSynLink *next = l->next;

                voice_free(v);
                l = next;
                freed = 1;
            }
        }
    }
    return freed;
}

/* The end of 0x106864 / 0x106A68: a stolen voice is taken off its lists and reused at once */
static Voice *voice_detach(Voice *v) {
    release_counts(v);
    unlink_node(&v->chLink);
    unlink_node(&v->coreLink);
    v->flags = 0;
    v->id = 0xFF;
    v->owner = 0xFF;
    return v;
}

static int core_allowed(uint8_t attr, int core) {
    uint8_t allow = (attr & 0x30) ? attr : 0x30;

    return (allow & (core ? 0x20 : 0x10)) != 0;
}

/* 0x106864: steal a playing MIDI voice of priority <= prio: released before damper-held before
   held, then the lowest priority, then the quietest; it is keyed off (its old sound releases
   normally before the new note keys on) */
static Voice *steal_active(uint16_t prio, uint8_t attr) {
    uint16_t bestClass = 0x300;
    uint16_t bestPrio = prio;
    uint16_t bestEnv = 0xFFFF;
    Voice *best = NULL;
    int c;

    for (c = 1; c >= 0; c--) {
        HSynLink *l;

        if (!core_allowed(attr, c)) {
            continue;
        }
        for (l = activeList[c].prev; l != &activeList[c]; l = l->prev) {
            Voice *v = VOICE_OF_CORE(l);
            uint16_t cls = v->flags & 0x300;

            if (v->owner != 0) {
                continue;
            }
            if (cls < bestClass) {
                if (v->priority <= prio) {
                    bestClass = cls;
                    bestPrio = v->priority;
                    bestEnv = v->envx;
                    best = v;
                }
            } else if (cls == bestClass) {
                if (v->priority == bestPrio) {
                    if (v->envx < bestEnv) {
                        bestEnv = v->envx;
                        best = v;
                    }
                } else if (v->priority < bestPrio) {
                    bestPrio = v->priority;
                    bestEnv = v->envx;
                    best = v;
                }
            }
        }
    }
    if (best == NULL) {
        return NULL;
    }
    koffMask[best->core] |= 1u << best->voice;
    return voice_detach(best);
}

/* 0x106A68: steal a voice still waiting to key on, the first of the lowest priority <= prio */
static Voice *steal_pending(uint16_t prio, uint8_t attr) {
    uint16_t bound = (uint16_t)(prio + 1);
    Voice *best = NULL;
    int c;

    for (c = 1; c >= 0; c--) {
        HSynLink *l;

        if (!core_allowed(attr, c)) {
            continue;
        }
        for (l = pendingList[c].prev; l != &pendingList[c]; l = l->prev) {
            Voice *v = VOICE_OF_CORE(l);

            if (v->owner == 0 && v->priority < bound) {
                bound = v->priority;
                best = v;
            }
        }
    }
    return best != NULL ? voice_detach(best) : NULL;
}

/* 0x1060CC: the port holds maxPolyphony voices: cut its voice of the lowest priority <= prio
   (released voices first, ties to the last found); 0 if there is none */
static int steal_in_port(HSynEnv *env, uint16_t prio) {
    Voice *best = NULL;
    uint16_t bound = prio;
    int n;
    int k;

    for (n = 0; n < 16; n++) {
        HSynLink *head = &env->channel[n].voices[2];
        HSynLink *l;

        for (l = head->next; l != head; l = l->next) {
            Voice *v = VOICE_OF_CH(l);

            if (v->priority <= bound) {
                bound = v->priority;
                best = v;
            }
        }
    }
    if (best == NULL) {
        for (n = 0; n < 16; n++) {
            for (k = 0; k < 2; k++) {
                HSynLink *head = &env->channel[n].voices[k];
                HSynLink *l;

                for (l = head->next; l != head; l = l->next) {
                    Voice *v = VOICE_OF_CH(l);

                    if (v->priority <= bound) {
                        bound = v->priority;
                        best = v;
                    }
                }
            }
        }
    }
    if (best == NULL) {
        return 0;
    }
    voice_kill(best);
    return 1;
}

/* 0x106BC8: a sample of an exclusive group cuts the channel's voices of that group */
static void cut_group(HSynChannel *ch, uint8_t group) {
    int k;

    for (k = 0; k < 3; k++) {
        HSynLink *head = &ch->voices[k];
        HSynLink *l = head->prev;

        while (l != head) {
            Voice *v = VOICE_OF_CH(l);
            HSynLink *prev = l->prev;

            if (v->group == group) {
                if (v->flags & VF_PENDING) {
                    unlink_node(&v->chLink);
                    voice_free_unlinked(v);
                } else {
                    if (k < 2) {
                        koffMask[v->core] |= 1u << v->voice;
                    }
                    sd_set_param((uint16_t)(voice_id(v) | VP_ADSR2), ADSR2_FAST_RELEASE);
                    voice_free(v);
                }
            }
            l = prev;
        }
    }
}

/* ---- Velocity curves ----------------------------------------------------------------------------- */
/* 0x108B3C */
static uint32_t square127(uint32_t x) {
    uint32_t r = ((x & 0xFF) * (x & 0xFF)) / 127;

    return r != 0 ? r & 0xFF : 1;
}

/* A velocity through the port's velocity map m (1-based; the maps are 126 bytes) */
static uint8_t vel_map(const HSynEnv *env, int m, uint8_t vel) {
    const uint8_t *map = (const uint8_t *)env->velocityMapTbl;

    return map[m * 126 + vel - 1];
}

/* 0x108C04: the velocity's gain (0..0x80) by curve type (map in bits 4-7, curve in bits 0-3) */
static uint8_t vel_abs(const HSynEnv *env, uint8_t vel, uint8_t type) {
    int m = (type >> 4) - 1;
    uint32_t v;

    if (m != -1 && m < (int32_t)env->velocityMapNum) {
        vel = vel_map(env, m, vel);
    }
    v = vel;
    switch ((type & 0xF) < 11 ? type & 0xF : 0) {
    case 0:
    case 6:
        return (uint8_t)v;
    case 1:
    case 7:
        return (uint8_t)(0x80 - v);
    case 2:
    case 8:
        return (uint8_t)square127(v);
    case 3:
    case 9:
        return (uint8_t)(0x80 - square127(v));
    case 4:
        return (uint8_t)(0x80 - square127((0x80 - v) & 0xFF));
    case 5:
        return (uint8_t)square127((0x80 - v) & 0xFF);
    default:
        /* 10: the PS2 jumps through a table entry that is not code */
        LOG_ONCE("MODHSYN: velocity curve 10 (a crash on the PS2)");
        return 0;
    }
}

static int32_t square14(int32_t x) {
    return shr_trunc(mul32(x, x), 14);
}

static int32_t div_guard(int32_t n, int32_t d) {
    if (d == 0) {
        /* the IOP traps (break 7) */
        LOG_ONCE("MODHSYN: division by zero (a trap on the IOP)");
        return 0;
    }
    return n / d;
}

/* 0x108A8C: how far vel is from centre on curve type, 16.16 (the LFO depths' velocity follow) */
static int32_t vel_rel(const HSynEnv *env, uint8_t vel, uint8_t centre, uint8_t type) {
    int m = (type >> 4) - 1;
    int32_t a;
    int32_t c;
    int32_t xa;
    int32_t xc;
    int32_t t;

    if (m != -1 && m < (int32_t)env->velocityMapNum) {
        vel = vel_map(env, m, vel);
        centre = vel_map(env, m, centre);
    }
    a = vel;
    c = centre;
    switch ((type & 0xF) < 11 ? type & 0xF : 0) {
    case 0:
        return (a - c) * 0x10000 / 126;
    case 1:
        return (c - a) * 0x10000 / 126;
    case 2:
        xa = (a - 1) * 0x8000 / 126;
        xc = (c - 1) * 0x8000 / 126;
        return shr_trunc((int32_t)((uint32_t)mul32(xa, xa) - (uint32_t)mul32(xc, xc)), 14);
    case 3:
        xa = (a - 1) * 0x8000 / 126;
        xc = (c - 1) * 0x8000 / 126;
        return (0x10000 - square14(xa)) - (0x10000 - square14(xc));
    case 4:
        xa = 0x10000 - (a - 1) * 0x8000 / 126;
        xc = 0x10000 - (c - 1) * 0x8000 / 126;
        return (0x10000 - square14(xa)) - (0x10000 - square14(xc));
    case 5:
        xa = 0x10000 - (a - 1) * 0x8000 / 126;
        xc = 0x10000 - (c - 1) * 0x8000 / 126;
        return square14(xa) - square14(xc);
    case 6:
        if (a == c) {
            return 0;
        }
        return div_guard((a - c) * 0x10000, c < a ? 0x7F - c : c - 1);
    case 7:
        if (a == c) {
            return 0;
        }
        return div_guard((c - a) * 0x10000, c < a ? 0x7F - c : c - 1);
    case 8:
        if (a == c) {
            return 0;
        }
        t = div_guard((a - c) * 0x8000, c < a ? 0x7F - c : c - 1);
        return c < a ? square14(t) : -(mul32(t, t) / 16384);
    case 9:
        if (a == c) {
            return 0;
        }
        t = div_guard((a - c) * 0x8000, c < a ? 0x7F - c : c - 1);
        return c < a ? 0x10000 - square14(0x8000 - t) : square14(t + 0x8000) - 0x10000;
    default:
        /* 10: calls 0x108B24, which returns its first argument */
        return a;
    }
}

/* ---- LFOs ---------------------------------------------------------------------------------------- */
static int16_t lfo_wave(Lfo *l, int wrapped) {
    switch (l->wave) {
    case WAVE_SAW_UP:
        return (int16_t)((int32_t)((uint32_t)(l->phase - 0x8000) << 15) >> 16);
    case WAVE_SAW_DOWN:
        return (int16_t)((int32_t)((uint32_t)(0xFFFF - l->phase - 0x8000) << 15) >> 16);
    case WAVE_TRIANGLE: {
        int32_t p = l->phase;

        if (p > 0xBFFF) {
            p -= 0x10000;
        } else if (p > 0x4000) {
            p = 0x8000 - p;
        }
        return (int16_t)p;
    }
    case WAVE_SQUARE:
        return l->phase > 0x7FFF ? 0x4000 : -0x4000;
    case WAVE_RANDOM:
        return wrapped ? (int16_t)((int16_t)hsyn_rand() >> 2) : l->out;
    case WAVE_TABLE: {
        /* 0x108D24: linear interpolation between the table's points */
        uint32_t p = (uint32_t)l->phase * l->tableLen;
        uint32_t i = p >> 16;
        uint32_t j = i + 1;
        int32_t a;
        int32_t x;

        if (l->table == NULL) {
            /* a custom wave that was not found, after one that was: the PS2 reads address 0 */
            LOG_ONCE("MODHSYN: LFO with no wave table (reads address 0 on the PS2)");
            return 0;
        }
        if (j >= l->tableLen) {
            j = 0;
        }
        a = l->table[i];
        x = a + (mul32(l->table[j] - a, (int32_t)(p & 0xFFFF)) >> 16);
        return (int16_t)((int32_t)((uint32_t)(x + (x < 0)) << 15) >> 16);
    }
    default:
        LOG_ONCE("MODHSYN: LFO with no wave (a jump to address 0 on the PS2)");
        return 0;
    }
}

/* 0x108F8C */
static void lfo_start(Lfo *l) {
    if (l->delay != 0) {
        l->state = 1;
        l->counter = l->delay;
    } else if (l->fadeStep != 0) {
        l->state = 2;
        l->fade = 0;
    } else {
        l->state = 3;
    }
}

static uint32_t ms_to_ticks(uint32_t ms) {
    if (tickUnit == 0) {
        LOG_ONCE("MODHSYN: tick of 0 ms (a trap on the IOP)");
        return 0;
    }
    return (ms * 256 + (tickUnit >> 1)) / tickUnit;
}

/* 0x108FD4: mode = the sample's LFO mode bits (bit 0/4: run from key-on, bit 1/5: run from
   key-off, bit 2/6: both), wave from the program */
static void lfo_setup(Lfo *l, const uint8_t *prog, const HSynEnv *env, uint8_t mode, uint16_t delayMs,
                      uint16_t fadeMs, int amp) {
    uint8_t wave = u8at(prog, amp ? 0xF : 0xE);
    uint32_t n;
    int32_t ph;
    uint32_t cycle;

    l->keyoffEnable = 0;
    l->state = 0;
    l->out = 0;
    if (!(mode & 0x77)) {
        return;
    }
    l->table = NULL;
    if (wave & 0x80) {
        const LfoWave *waves = (const LfoWave *)env->lfoWaveTbl;
        uint32_t i;

        for (i = 0; (int32_t)i < (int32_t)env->lfoWaveNum; i++) {
            /* the PS2 compares the ids with wave & 0x80, not with the wave number */
            if (waves[i].id == 0x80) {
                l->table = waves[i].data;
                l->wave = WAVE_TABLE;
                l->tableLen = waves[i].len;
                break;
            }
        }
        /* not found: a length left by an earlier note keeps the earlier wave, with no table */
        if (l->table == NULL && l->tableLen == 0) {
            return;
        }
    } else {
        switch (wave) {
        case 1:
            l->wave = WAVE_SAW_UP;
            break;
        case 2:
            l->wave = WAVE_SAW_DOWN;
            break;
        case 3:
            l->wave = WAVE_TRIANGLE;
            break;
        case 4:
            l->wave = WAVE_SQUARE;
            break;
        case 5:
            l->wave = WAVE_RANDOM;
            break;
        case 6:
            l->table = sine200;
            l->tableLen = 200;
            l->wave = WAVE_TABLE;
            break;
        default:
            return;
        }
    }
    l->delay = (uint16_t)ms_to_ticks(delayMs);
    n = ms_to_ticks(fadeMs);
    if (n != 0) {
        n = 0x10000u / n;
        if (n == 0) {
            n = 1;
        }
    }
    l->fadeStep = n;
    ph = mul32(u8at(prog, amp ? 0x13 : 0x12) << 8, (int16_t)hsyn_rand());
    ph = shr_trunc(ph, 16) + (u8at(prog, amp ? 0x11 : 0x10) << 8);
    if (ph < 0) {
        ph += 0x10000;
    } else if (ph > 0xFFFF) {
        ph -= 0x10000;
    }
    l->phase = ph;
    cycle = ms_to_ticks(rd16(prog, amp ? 0x16 : 0x14));
    if (cycle == 0) {
        /* divu by 0: the IOP traps (break 7) */
        LOG_ONCE("MODHSYN: LFO cycle of 0 ticks (a trap on the IOP)");
        l->step = 0;
    } else {
        l->step = 0x10000u / cycle;
    }
    if (mode & 0x66) {
        l->keyoffEnable = 1;
    }
    if (mode & 0x55) {
        lfo_start(l);
    }
}

/* 0x108DCC */
static int16_t lfo_tick(Lfo *l) {
    int32_t w = 0;
    uint8_t state = l->state;

    if (state == 0 || state >= 6) {
        l->out = 0;
        return 0;
    }
    if (state >= 2) {
        int wrapped = 0;
        int32_t p = l->phase + (int32_t)l->step;

        if (p > 0xFFFF) {
            p -= 0x10000;
            wrapped = 1;
        }
        l->phase = p;
        w = lfo_wave(l, wrapped);
    }
    switch (state) {
    case 1:
        if (--l->counter == 0) {
            if (l->fadeStep != 0) {
                l->fade = 0;
                l->state = 2;
            } else {
                l->state = 3;
            }
        }
        w = 0;
        break;
    case 2:
        w = (int16_t)shr_trunc(mul32((int16_t)w, l->fade), 16);
        l->fade += (int32_t)l->fadeStep;
        if (l->fade > 0xFFFF) {
            l->state = 3;
        }
        break;
    case 4:
        if (--l->counter == 0) {
            if (l->fadeStep != 0) {
                l->fade = 0x10000;
                l->state = 5;
            } else {
                l->state = 6;
            }
        }
        break;
    case 5:
        w = (int16_t)shr_trunc(mul32((int16_t)w, l->fade), 16);
        l->fade -= (int32_t)l->fadeStep;
        if (l->fade <= 0) {
            l->state = 6;
        }
        break;
    default:
        break;
    }
    l->out = (int16_t)w;
    return (int16_t)w;
}

/* 0x109318 */
static void lfo_keyoff(Lfo *l) {
    if (!l->keyoffEnable) {
        return;
    }
    switch (l->state) {
    case 0:
        lfo_start(l);
        break;
    case 2:
    case 3:
        if (l->delay != 0) {
            l->state = 4;
            l->counter = l->delay;
        } else if (l->fadeStep != 0) {
            l->state = 5;
            l->fade = 0x10000;
        } else {
            l->state = 6;
        }
        break;
    default:
        l->state = 6;
        break;
    }
}

/* ---- Pitch --------------------------------------------------------------------------------------- */
/* 0x1042F8: the bend's offset in 1/128 semitones, by the split's bend ranges */
static void bend_update(Voice *v, int16_t bend) {
    int32_t range;

    if (v->split == NULL) {
        return;
    }
    range = bend >= 0 ? rd16(v->split, 8) : rd16(v->split, 6);
    v->bendOffset = shr_trunc(range * bend, 13);
}

/* 0x104158: writes PITCH when the offset (LFO + bend + portamento) has changed */
static void pitch_update(Voice *v) {
    int32_t o = v->plfo.out;
    /* (a sound-effect voice adds its pitch sweep: there are none) */
    int32_t t = shr_trunc(o * (o >= 0 ? v->plfoDepth[0] : v->plfoDepth[1]), 14) + v->bendOffset + v->portaOffset;
    int32_t centre;
    int32_t note;
    uint32_t pitch;

    if (t == v->pitchCache) {
        return;
    }
    v->pitchCache = t;
    if (v->waveType == 1) {
        centre = v->key;
        note = v->key + s8at(v->prog, 5);
        t += s8at(v->prog, 6);
    } else {
        centre = u8at(v->smpl, 0xB);
        note = v->key + s8at(v->prog, 8) + s8at(v->split, 0x12);
        t += s8at(v->prog, 9) + s8at(v->smpl, 0xC) + s8at(v->split, 0x13);
    }
    if (t < 0) {
        int32_t q = (t + 0x7F) >> 7;

        note += q;
        t -= q << 7;
        if (t != 0) {
            t += 0x80;
            note -= 1;
        }
    }
    pitch = sd_note2pitch((uint16_t)centre, 0, (uint16_t)note, (int16_t)t);
    pitch = (pitch * v->rateRatio) >> 16;
    if ((int32_t)pitch >= 0x4000) {
        pitch = 0x3FFF;
    }
    if (v->pitchOverride != 0) {
        pitch = v->pitchOverride;
        v->pitchCache = v->pitchOverride;
    }
    sd_set_param((uint16_t)(voice_id(v) | VP_PITCH), (uint16_t)pitch);
}

/* ---- Volume and pan ------------------------------------------------------------------------------ */
/* 0x1044F0 */
static void volume_ch_update(Voice *v) {
    v->volume = (uint16_t)(((uint32_t)v->ch->volume * v->expression * v->volBase) >> 14);
}

/* 0x10434C: writes VOLL/VOLR when they have changed */
static void volume_update(Voice *v) {
    int32_t a = shr_trunc(mul32(v->volume, v->env->volume), 8);
    int32_t o = v->alfo.out;
    int32_t l = shr_trunc(o * (o >= 0 ? v->alfoDepth[0] : v->alfoDepth[1]), 7);
    int32_t t = shr_trunc(mul32(a, l + 0x4000), 14);
    uint32_t left;
    uint32_t right;
    uint16_t id = voice_id(v);

    if (t < 0) {
        t = 0;
    }
    left = (uint32_t)mul32(v->panL, t) >> 7;
    if (left >= 0x4000) {
        left = 0x3FFF;
    }
    right = (uint32_t)mul32(v->panR, t) >> 7;
    if (right >= 0x4000) {
        right = 0x3FFF;
    }
    if (left == v->lastL && right == v->lastR) {
        return;
    }
    v->lastL = (uint16_t)left;
    v->lastR = (uint16_t)right;
    if (v->phase) {
        sd_set_param((uint16_t)(id | VP_VOLL), (uint16_t)left);
        sd_set_param((uint16_t)(id | VP_VOLR), (uint16_t)right);
    } else {
        sd_set_param((uint16_t)(id | VP_VOLL), (uint16_t)((0u - left) & 0x7FFF));
        sd_set_param((uint16_t)(id | VP_VOLR), (uint16_t)((0u - right) & 0x7FFF));
    }
}

/* 0x1046E4: the program's, split's and sample's pans and the key follow (0x40 centre). In
   surround the sum wraps and may put the voice behind (phase inverted). */
static void pan_static(Voice *v, int32_t follow) {
    int32_t p = s8at(v->prog, 7);
    int32_t s = s8at(v->split, 0x11);
    int32_t m = s8at(v->smpl, 0xD);
    int32_t x;

    if (v->surround) {
        x = p - 0x80 + (s + m + follow);
        while (x >= 0x80) {
            x -= 0xFF;
        }
        while (x < -0x80) {
            x += 0xFF;
        }
        v->panSum = (int16_t)x;
        return;
    }
    if (p < 0) {
        p = -p;
    }
    if (s < 0) {
        s = -s;
    }
    if (m < 0) {
        m = -m;
    }
    x = p - 0x80 + (s + m + follow);
    if (x < 0) {
        x = 0;
    } else if (x >= 0x80) {
        x = 0x7F;
    }
    v->phase = 1;
    v->panSum = (int16_t)x;
}

/* 0x1047B8: the channel's (or F9/FD) pan a on top: linear gains, the far side turned down */
static void pan_apply(Voice *v, int8_t a) {
    int32_t aa = a;
    int32_t x;

    v->panL = 0x80;
    v->panR = 0x80;
    v->panPos = 0;
    if (mono) {
        return;
    }
    if (aa >= 0x40) {
        aa = 0x3F;
    } else if (aa < -0x3F) {
        aa = -0x3F;
    }
    if (v->surround) {
        int32_t y = v->panSum;
        int ph = 1;

        if (y < 0) {
            y = -y;
            ph = 0;
        }
        y += aa;
        if (y < 0) {
            ph ^= 1;
            y = -y;
        } else if (y >= 0x80) {
            ph ^= 1;
            y = 0x100 - y;
        }
        v->phase = (uint8_t)ph;
        x = y - 0x40;
    } else {
        x = aa + v->panSum - 0x40;
    }
    if (x > 0) {
        v->panL = x >= 0x3F ? 0 : (uint8_t)(((0x3F - x) << 7) / 63);
    } else if (x < 0) {
        v->panR = x < -0x3E ? 0 : (uint8_t)(((x + 0x3F) << 7) / 63);
    }
    v->panPos = (int8_t)x;
}

/* ---- Note setup ---------------------------------------------------------------------------------- */
/* 0x104528 */
static int32_t vel_follow(Voice *v, int32_t depth, uint8_t centre, uint8_t type) {
    return shr_trunc(mul32(vel_rel(v->env, v->velocity, centre, type), depth), 16);
}

static uint32_t fade_div(uint32_t num, uint32_t den) {
    if (den == 0) {
        /* divu by 0: the IOP traps (break 7) */
        LOG_ONCE("MODHSYN: crossfade over no range (a trap on the IOP)");
        return 0;
    }
    return num / den;
}

/* 0x10490C: velocity and key crossfades, 16.16 (unsigned divisions) */
static uint32_t crossfade(Voice *v) {
    const uint8_t *m = v->smpl;
    const uint8_t *s = v->split;
    uint32_t g = 0x10000;
    uint32_t edge;

    if (v->velocity >= u8at(m, 3)) {
        if ((u8at(m, 4) & 0x80) && (u8at(m, 4) & 0x7F) != u8at(m, 3)) {
            edge = (u8at(m, 4) & 0x7Fu) + 1;
            g = fade_div((edge - v->velocity) << 16, edge - u8at(m, 3));
        }
    } else if ((u8at(m, 2) & 0x80) && (u8at(m, 2) & 0x7F) != u8at(m, 3)) {
        edge = (u8at(m, 2) & 0x7Fu) - 1;
        g = fade_div((v->velocity - edge) << 16, u8at(m, 3) - edge);
    }
    if (v->key >= u8at(s, 3)) {
        if ((u8at(s, 4) & 0x80) && (u8at(s, 4) & 0x7F) != u8at(s, 3)) {
            edge = (u8at(s, 4) & 0x7Fu) + 1;
            g = fade_div(g * (edge - v->key), edge - u8at(s, 3));
        }
    } else if ((u8at(s, 2) & 0x80) && (u8at(s, 2) & 0x7F) != u8at(s, 3)) {
        edge = (u8at(s, 2) & 0x7Fu) - 1;
        g = fade_div(g * (v->key - edge), u8at(s, 3) - edge);
    }
    return g;
}

/* 0x10458C: amplitude LFO depth with the breath controller */
static void alfo_breath(Voice *v, uint8_t cc) {
    v->alfoDepth[0] = (int8_t)clamp8(shr_trunc(cc * s8at(v->prog, 0x22), 7) + v->alfoBase[0]);
    v->alfoDepth[1] = (int8_t)clamp8(shr_trunc(cc * s8at(v->prog, 0x23), 7) + v->alfoBase[1]);
}

/* 0x104634: pitch LFO depth with the modulation wheel */
static void plfo_modulation(Voice *v, uint8_t cc) {
    v->plfoDepth[0] = (int16_t)clamp16(shr_trunc(cc * s16at(v->prog, 0x1C), 7) + v->plfoBase[0]);
    v->plfoDepth[1] = (int16_t)clamp16(shr_trunc(cc * s16at(v->prog, 0x1E), 7) + v->plfoBase[1]);
}

/* 0x103904 */
static void note_setup(Voice *v) {
    HSynChannel *ch = v->ch;
    const HSynEnv *env = ch->env;
    const uint8_t *p = v->prog;
    const uint8_t *s = v->split;
    const uint8_t *m = v->smpl;
    int32_t kf;
    int32_t vf;
    int32_t t;
    int8_t a;

    lfo_setup(&v->plfo, p, env, u8at(m, 0x28) & 7, rd16(m, 0x20), rd16(m, 0x22), 0);
    kf = ((int32_t)v->key - u8at(s, 0xB)) * s8at(s, 0xA) / 12;
    vf = vel_follow(v, s8at(m, 5), u8at(m, 6), u8at(m, 7));
    v->plfoBase[0] = (int16_t)clamp16(s16at(p, 0x18) + kf + vf);
    v->plfoBase[1] = (int16_t)clamp16(s16at(p, 0x1A) + kf + vf);
    lfo_setup(&v->alfo, p, env, u8at(m, 0x28) & 0x70, rd16(m, 0x24), rd16(m, 0x26), 1);
    kf = ((int32_t)v->key - u8at(s, 0xD)) * s8at(s, 0xC) / 12;
    vf = vel_follow(v, s8at(m, 8), u8at(m, 9), u8at(m, 0xA));
    v->alfoBase[0] = (int8_t)clamp8(s8at(p, 0x20) + kf + vf);
    v->alfoBase[1] = (int8_t)clamp8(s8at(p, 0x21) + kf + vf);
    alfo_breath(v, ch->breath);
    plfo_modulation(v, ch->modulation);

    t = mul32(mul32(mul32(u8at(p, 6), u8at(s, 0x10)), u8at(m, 0x10)), vel_abs(env, v->velocity, v->velCurve));
    t = shr_trunc(t, 14);
    v->volBase = (uint16_t)(((uint32_t)t * crossfade(v)) >> 16);

    v->surround = u8at(p, 0xC) & 1;
    pan_static(v, (((int32_t)v->key - u8at(s, 0xF)) * s8at(s, 0xE) + ((int32_t)v->key - u8at(p, 0xB)) * s8at(p, 0xA)) / 12);
    if (v->exPan & 0x80) {
        a = ch->pan;
    } else {
        a = (int8_t)(v->exPan - 0x40);
        if (a < -0x3F) {
            a = -0x3F;
        }
    }
    pan_apply(v, a);
    /* an F9 expression is used as sent (0..127), not scaled like CC11 */
    v->expression = v->exExpression != 0xFF ? v->exExpression : ch->expression;
    volume_ch_update(v);
    v->lastL = 0xFFFF;
    v->lastR = 0xFFFF;
    if (v->vagi != NULL) {
        v->pitchCache = 0x7FFFFFFF;
        v->rateRatio = ((uint32_t)rd16(v->vagi, 4) << 16) / 48000u;
        bend_update(v, (v->exBend & 0x8000) ? ch->bend : (int16_t)(v->exBend - 0x2000));
    }
}

/* 0x104ECC: glide from the channel's last key */
static void porta_setup(Voice *v) {
    int32_t d = ((int32_t)v->ch->portaKey - v->key) << 7;
    int32_t r;

    v->portaOffset = d;
    if (d < 0) {
        d = -d;
        v->flags |= VF_PORTA_UP;
    } else {
        v->flags |= VF_PORTA_DOWN;
    }
    r = (int32_t)(((uint32_t)d * tickUnit) / (uint32_t)(int32_t)v->ch->portaTime);
    v->portaRate = (uint16_t)shr_trunc(r, 8);
}

/* ---- Key-on -------------------------------------------------------------------------------------- */
/* 0x10375C: an envelope rate moved by the key's distance from centre */
static uint16_t adsr_follow(uint16_t reg, int32_t depth, int32_t dkey, int32_t mask, int shift) {
    int32_t d = dkey * depth / 12;
    int32_t f;

    if (d == 0) {
        return reg;
    }
    f = ((reg >> shift) & mask) + d;
    if (f < 0) {
        f = 0;
    } else if (f > mask) {
        f = mask;
    }
    return (uint16_t)((reg & ~(mask << shift)) | (f << shift));
}

/* 0x103CC0 */
static void key_on(Voice *v) {
    const uint8_t *m = v->smpl;
    uint16_t id = voice_id(v);
    uint32_t bit = 1u << v->voice;
    int c = v->core;
    uint8_t attr;

    volume_update(v);
    if (v->vagi != NULL) {
        uint16_t adsr;

        nonMask[c] &= ~bit;
        pitch_update(v);
        sd_set_addr((uint16_t)(id | VA_SSA), v->bdSpu + rd32(v->vagi, 0));
        adsr = rd16(m, 0x12);
        if (s8at(m, 0x16) != 0) {
            adsr = adsr_follow(adsr, s8at(m, 0x16), v->key - u8at(m, 0x17), 0x7F, 8);
        }
        if (s8at(m, 0x18) != 0) {
            adsr = adsr_follow(adsr, s8at(m, 0x18), v->key - u8at(m, 0x19), 0xF, 4);
        }
        if (s8at(m, 0x1E) != 0) {
            adsr = adsr_follow(adsr, s8at(m, 0x1E), v->key - u8at(m, 0x1F), 0xF, 0);
        }
        sd_set_param((uint16_t)(id | VP_ADSR1), adsr);
        adsr = rd16(m, 0x14);
        if (s8at(m, 0x1A) != 0) {
            adsr = adsr_follow(adsr, s8at(m, 0x1A), v->key - u8at(m, 0x1B), 0x7F, 6);
        }
        if (s8at(m, 0x1C) != 0) {
            adsr = adsr_follow(adsr, s8at(m, 0x1C), v->key - u8at(m, 0x1D), 0x1F, 0);
        }
        sd_set_param((uint16_t)(id | VP_ADSR2), adsr);
        if (!(u8at(v->vagi, 6) & 1)) {
            v->flags |= VF_ONESHOT;
        }
    } else {
        sd_set_core_attr((uint16_t)(c | CA_NOISE_CLK), v->key >= 0x40 ? 0x3F : v->key);
        nonMask[c] |= bit;
    }
    sd_set_switch((uint16_t)(SW_NON | c), nonMask[c]);
    attr = u8at(m, 0x29);
    vmixl[c] = (attr & 1) ? vmixl[c] | bit : vmixl[c] & ~bit;
    vmixr[c] = (attr & 2) ? vmixr[c] | bit : vmixr[c] & ~bit;
    vmixel[c] = (attr & 4) ? vmixel[c] | bit : vmixel[c] & ~bit;
    vmixer[c] = (attr & 8) ? vmixer[c] | bit : vmixer[c] & ~bit;
    sd_set_switch((uint16_t)(SW_VMIXL | c), vmixl[c]);
    sd_set_switch((uint16_t)(SW_VMIXR | c), vmixr[c]);
    sd_set_switch((uint16_t)(SW_VMIXEL | c), vmixel[c]);
    sd_set_switch((uint16_t)(SW_VMIXER | c), vmixer[c]);
    konMask[c] |= bit;
    v->grace = 3;
    v->flags &= (uint16_t)~VF_PENDING;
    stat_state(v, 0x8F, 0x20);
}

/* ---- Note-on and note-off ------------------------------------------------------------------------ */
/* 0x106250: every split of the program that holds the key, every sample of its velocity range
   that holds the velocity, gets a voice (layers); id is 0xFF for a MIDI note-on */
static void note_on(HSynEnv *env, HSynChannel *ch, unsigned port, uint8_t key, uint8_t vel, uint32_t id) {
    const uint8_t *prog = ch->program;
    HSynBank *bank = ch->bank;
    const uint8_t *split;
    int splits;
    int started = 0;

    if (prog == NULL) {
        return;
    }
    if (env->waveType == 1) {
        LOG_ONCE("MODHSYN: note-on on a Setb bank port (waveType 1), not implemented");
        return;
    }
    if (bank->sset == NULL || bank->smpl == NULL) {
        /* the PS2 reads from address 0 */
        LOG_ONCE("MODHSYN: note-on with a bank that has no Sset or Smpl chunk");
        return;
    }
    split = prog + rd32(prog, 0);
    splits = u8at(prog, 4);
    for (; splits > 0; splits--, split += 0x14) {
        const uint8_t *sset;
        int k;

        if (key < (u8at(split, 2) & 0x7F) || (u8at(split, 4) & 0x7F) < key) {
            continue;
        }
        sset = chunk_entry(bank->sset, rd16(split, 0));
        if (sset == NULL || vel < u8at(sset, 1) || u8at(sset, 2) < vel) {
            continue;
        }
        for (k = 0; k < u8at(sset, 3); k++) {
            const uint8_t *smpl = chunk_entry(bank->smpl, rd16(sset, 4 + 2 * (uint32_t)k));
            const uint8_t *vagi;
            uint16_t prio;
            uint8_t attr;
            Voice *v;

            if (smpl == NULL || vel < (u8at(smpl, 2) & 0x7F) || (u8at(smpl, 4) & 0x7F) < vel) {
                continue;
            }
            /* a sample past the Vagi chunk plays noise; one with no data is skipped */
            vagi = bank->vagi != NULL ? chunk_entry(bank->vagi, rd16(smpl, 0)) : NULL;
            if (vagi != NULL && rd32(vagi, 0) == 0xFFFFFFFFu) {
                continue;
            }
            if (u8at(smpl, 0xE) != 0) {
                cut_group(ch, u8at(smpl, 0xE));
            }
            prio = (uint16_t)(u8at(smpl, 0xF) + env->priority);
            if (env->voices >= env->maxPolyphony && !steal_in_port(env, prio)) {
                /* the whole note-on ends here, and the F9 settings are kept */
                return;
            }
            attr = u8at(smpl, 0x29);
            v = voice_alloc(attr);
            if (v == NULL && pollPending) {
                pollPending = 0;
                if (poll_voices()) {
                    v = voice_alloc(attr);
                }
            }
            if (v == NULL) {
                v = steal_active(prio, attr);
            }
            if (v == NULL) {
                v = steal_pending(prio, attr);
            }
            if (v == NULL) {
                continue;
            }
            v->flags = VF_HELD | VF_PENDING;
            v->env = env;
            v->ch = ch;
            v->prog = prog;
            v->smpl = smpl;
            v->vagi = vagi;
            v->split = split;
            v->envx = 0;
            v->portaOffset = 0;
            v->priority = prio;
            v->key = key;
            v->id = id;
            v->pitchOverride = 0;
            v->bdSpu = bank->bdSpu;
            v->velocity = vel;
            v->group = u8at(smpl, 0xE);
            v->velCurve = u8at(sset, 0);
            v->exBend = ch->exBend;
            v->exExpression = ch->exExpression;
            v->owner = 0;
            v->exPan = ch->exPan;
            v->waveType = env->waveType;
            env->voices++;
            midiVoices++;
            link_front(&v->chLink, &ch->voices[0]);
            started = 1;
            if (!(ch->portaKey & 0x80) && ch->portaTime != 0) {
                porta_setup(v);
            }
            note_setup(v);
            link_front(&v->coreLink, &pendingList[v->core]);
            if (voiceStat != NULL) {
                voiceStat->voice_state[v->core][v->voice] = (uint8_t)(0x90 | port);
            }
        }
    }
    if (started) {
        ch->exBend = 0xFFFF;
        ch->exExpression = 0xFF;
        ch->exPan = 0xFF;
    }
}

/* 0x105954: the key's held voices (of that id, or any for 0xFF) release, or wait for the damper */
static void note_off(HSynChannel *ch, uint8_t key, uint32_t id) {
    HSynLink *head = &ch->voices[0];
    HSynLink *l = head->prev;

    if (ch->portaKey <= 0x80) {
        ch->portaKey = key;
    }
    while (l != head) {
        Voice *v = VOICE_OF_CH(l);
        HSynLink *prev = l->prev;

        if (v->key == key && (v->id == id || id == 0xFF)) {
            uint16_t flags = v->flags;

            unlink_node(&v->chLink);
            v->flags = flags & (uint16_t)~(VF_HELD | VF_DAMPER);
            if (ch->damper != 0) {
                v->flags |= VF_DAMPER;
                link_front(&v->chLink, &ch->voices[1]);
            } else if (flags & VF_PENDING) {
                voice_free_unlinked(v);
            } else {
                lfo_keyoff(&v->plfo);
                lfo_keyoff(&v->alfo);
                koffMask[v->core] |= 1u << v->voice;
                link_front(&v->chLink, &ch->voices[2]);
                stat_state(v, 0x8F, 0x30);
            }
        }
        l = prev;
    }
}

/* ---- Controllers --------------------------------------------------------------------------------- */
/* Each voice on the channel's three lists */
#define FOR_CHANNEL_VOICES(ch, v)                                                         \
    for (int k_ = 0; k_ < 3; k_++)                                                        \
        for (HSynLink *h_ = &(ch)->voices[k_], *l_ = h_->next; l_ != h_; l_ = l_->next) \
            for (Voice *v = VOICE_OF_CH(l_); v != NULL; v = NULL)

static uint8_t scale127(uint8_t value) {
    return (uint8_t)(((uint32_t)value << 7) / 127);
}

/* 0x100758 */
static void pitch_bend(HSynChannel *ch, uint8_t msb, uint8_t lsb) {
    int16_t bend = (int16_t)((lsb + (msb << 7)) - 0x2000);

    if (ch->bend == bend) {
        return;
    }
    ch->bend = bend;
    FOR_CHANNEL_VOICES(ch, v) {
        bend_update(v, bend);
    }
}

/* 0x1008D8: CC7 */
static void cc_volume(HSynChannel *ch, uint8_t value) {
    uint8_t x = scale127(value);

    if (ch->volume == x) {
        return;
    }
    ch->volume = x;
    FOR_CHANNEL_VOICES(ch, v) {
        volume_ch_update(v);
    }
}

/* 0x100934: CC11 */
static void cc_expression(HSynChannel *ch, uint8_t value) {
    uint8_t x = scale127(value);

    if (ch->expression == x) {
        return;
    }
    ch->expression = x;
    FOR_CHANNEL_VOICES(ch, v) {
        v->expression = ch->expression;
        volume_ch_update(v);
    }
}

/* 0x100990: CC2 */
static void cc_breath(HSynChannel *ch, uint8_t value) {
    uint8_t x = scale127(value);

    if (ch->breath == x) {
        return;
    }
    ch->breath = x;
    FOR_CHANNEL_VOICES(ch, v) {
        alfo_breath(v, x);
    }
}

/* 0x100A58: CC1 */
static void cc_modulation(HSynChannel *ch, uint8_t value) {
    uint8_t x = scale127(value);

    if (ch->modulation == x) {
        return;
    }
    ch->modulation = x;
    FOR_CHANNEL_VOICES(ch, v) {
        plfo_modulation(v, x);
    }
}

/* 0x100B78: CC10 */
static void cc_pan(HSynChannel *ch, uint8_t value) {
    int8_t a = (int8_t)(value - 0x40);

    if (a < -0x3F) {
        a = -0x3F;
    }
    if (ch->pan == a) {
        return;
    }
    ch->pan = a;
    FOR_CHANNEL_VOICES(ch, v) {
        pan_apply(v, a);
    }
}

/* 0x100B38: CC65 */
static void cc_portamento(HSynChannel *ch, uint8_t value) {
    if (value != 0) {
        if (ch->portaKey == 0xFF) {
            ch->portaKey = 0x80;
            ch->portaControl = 0x80;
        }
    } else {
        ch->portaKey = 0xFF;
    }
}

/* 0x105C34: CC64; letting the damper go releases the voices it held */
static void cc_damper(HSynChannel *ch, uint8_t value) {
    HSynLink *head = &ch->voices[1];

    if (value != 0) {
        ch->damper = value;
        return;
    }
    if (ch->damper == 0) {
        return;
    }
    ch->damper = 0;
    while (head->next != head) {
        Voice *v = VOICE_OF_CH(head->next);

        unlink_node(&v->chLink);
        if (v->flags & VF_PENDING) {
            voice_free_unlinked(v);
            continue;
        }
        lfo_keyoff(&v->plfo);
        lfo_keyoff(&v->alfo);
        koffMask[v->core] |= 1u << v->voice;
        link_front(&v->chLink, &ch->voices[2]);
        v->flags &= (uint16_t)~(VF_HELD | VF_DAMPER);
        stat_state(v, 0x8F, 0x30);
    }
}

/* 0x105D74: CC123; the damper does not hold them, and the LFOs carry on */
static void all_notes_off(HSynChannel *ch) {
    int k;

    for (k = 0; k < 2; k++) {
        HSynLink *head = &ch->voices[k];

        while (head->prev != head) {
            Voice *v = VOICE_OF_CH(head->prev);

            unlink_node(&v->chLink);
            if (v->flags & VF_PENDING) {
                voice_free_unlinked(v);
                continue;
            }
            v->flags &= (uint16_t)~(VF_HELD | VF_DAMPER);
            koffMask[v->core] |= 1u << v->voice;
            link_front(&v->chLink, &ch->voices[2]);
            stat_state(v, 0x8F, 0x30);
        }
    }
}

/* 0x105EB4: CC120 */
static void all_sound_off(HSynChannel *ch) {
    int k;

    for (k = 0; k < 3; k++) {
        HSynLink *head = &ch->voices[k];

        while (head->prev != head) {
            voice_kill(VOICE_OF_CH(head->prev));
        }
    }
}

/* 0x100F90: CC121 (volume and pan are reset too) */
static void reset_controllers(HSynChannel *ch) {
    cc_damper(ch, 0);
    pitch_bend(ch, 0x40, 0);
    cc_breath(ch, 0);
    cc_modulation(ch, 0);
    cc_volume(ch, 0x7F);
    cc_pan(ch, 0x40);
    cc_expression(ch, 0x7F);
    cc_portamento(ch, 0);
}

/* sceSdGetEffectAttr: sd.h has no wrapper, so this read is not in the libsd trace */
static void get_effect_attr(int core, spu2_sd_effect_attr *attr) {
    memset(attr, 0, sizeof(*attr));
    spu2_sd_get_effect_attr(core, attr);
}

/* 0x101D04: NRPN 2 (LSB 0x00 core 0, 0x10 core 1) sets the reverb mode, clearing its work area */
static void nrpn_effect_mode(uint8_t lsb, uint8_t data) {
    spu2_sd_effect_attr attr;
    int core;

    if (data >= 10 || (lsb != 0 && lsb != 0x10)) {
        return;
    }
    core = (lsb & 0x10) != 0;
    get_effect_attr(core, &attr);
    attr.mode = data | 0x100;
    sd_set_effect_attr(core, &attr);
}

/* 0x101D74: NRPN 3, LSB bit 4 the core, bits 0-3: 0/1 depth (+/-), 2 delay, 3 feedback */
static void nrpn_effect_param(uint8_t lsb, uint8_t data) {
    spu2_sd_effect_attr attr;
    int core;

    if (lsb & 0xE0) {
        return;
    }
    core = (lsb & 0x10) != 0;
    get_effect_attr(core, &attr);
    switch (lsb & 0xF) {
    case 0:
        attr.depth_L = attr.depth_R = (short)(data << 8);
        break;
    case 1:
        attr.depth_L = attr.depth_R = (short)-(data << 8);
        break;
    case 2:
        attr.delay = data;
        break;
    case 3:
        attr.feedback = data;
        break;
    default:
        break;
    }
    sd_set_effect_attr(core, &attr);
}

/* 0x101010 */
static void control_change(HSynChannel *ch, uint8_t cc, uint8_t value) {
    switch (cc) {
    case 0x00:
        ch->bankNo = value;
        break;
    case 0x01:
        cc_modulation(ch, value);
        break;
    case 0x02:
        cc_breath(ch, value);
        break;
    case 0x05:
        ch->portaTime = (int16_t)(value * 20);
        break;
    case 0x06:
        ch->nrpnData = value;
        if (ch->nrpnLsb != 0xFF) {
            if (ch->nrpnMsb == 2) {
                nrpn_effect_mode(ch->nrpnLsb, ch->nrpnData);
            } else if (ch->nrpnMsb == 3) {
                nrpn_effect_param(ch->nrpnLsb, ch->nrpnData);
            }
        }
        ch->nrpnData = 0xFF;
        ch->nrpnLsb = 0xFF;
        ch->nrpnMsb = 0xFF;
        break;
    case 0x07:
        cc_volume(ch, value);
        break;
    case 0x0A:
        cc_pan(ch, value);
        break;
    case 0x0B:
        cc_expression(ch, value);
        break;
    case 0x40:
        cc_damper(ch, value);
        break;
    case 0x41:
        cc_portamento(ch, value);
        break;
    case 0x54:
        ch->portaControl = value;
        break;
    case 0x62:
        ch->nrpnLsb = value;
        break;
    case 0x63:
        ch->nrpnMsb = value;
        ch->nrpnData = 0xFF;
        ch->nrpnLsb = 0xFF;
        break;
    case 0x78:
        all_sound_off(ch);
        break;
    case 0x79:
        reset_controllers(ch);
        break;
    case 0x7B:
        all_notes_off(ch);
        break;
    default:
        break;
    }
}

/* 0x1006BC: the program of the channel's bank; none if anything is missing */
static void program_change(HSynEnv *env, HSynChannel *ch, uint8_t prog) {
    const uint8_t *entry = NULL;
    HSynBank *bank = NULL;

    if (ch->bankNo < 16) {
        bank = &env->bank[ch->bankNo];
        if (bank->prog != NULL && rd32(bank->prog, 0xC) >= prog) {
            int32_t offset = (int32_t)rd32(bank->prog, 0x10 + 4u * prog);

            if (offset != -1) {
                entry = bank->prog + offset;
                if (u8at(entry, 4) == 0) {
                    entry = NULL;
                }
            }
        }
    }
    ch->program = entry;
    ch->bank = entry != NULL ? bank : NULL;
}

/* ---- The input streams --------------------------------------------------------------------------- */
/* 0x1011C8 */
static void channel_message(HSynEnv *env, unsigned port, uint8_t status, uint8_t d1, uint8_t d2) {
    HSynChannel *ch = &env->channel[status & 0xF];

    switch (status & 0xF0) {
    case 0x90:
        if (d2 != 0) {
            note_on(env, ch, port, d1, d2, 0xFF);
            break;
        }
        note_off(ch, d1, 0xFF);
        break;
    case 0x80:
        note_off(ch, d1, 0xFF);
        break;
    case 0xB0:
        control_change(ch, d1, d2);
        break;
    case 0xC0:
        program_change(env, ch, d1);
        break;
    case 0xE0:
        pitch_bend(ch, d2, d1);
        break;
    default:
        /* Ax, Dx: ignored */
        break;
    }
}

/* Whether an FD message's (key, id) selects a voice: id 0x7F any id, key bit 7 any key */
static int fd_matches(const Voice *v, uint8_t key, uint8_t id) {
    return (id == 0x7F || v->id == id) && ((key & 0x80) || v->key == key);
}

/* 0x1012F8: F9 sets a channel's expression, pan or bend for its next note-on; FD changes the
   expression, pan or bend of voices already playing, or plays a note with an id */
static void special_message(HSynEnv *env, unsigned port, uint8_t status, const uint8_t *b) {
    HSynChannel *ch = &env->channel[b[1] & 0xF];

    if (status == 0xF9) {
        switch (b[0]) {
        case 0:
            ch->exExpression = b[2];
            break;
        case 1:
            ch->exPan = b[2];
            break;
        case 2:
            ch->exBend = (uint16_t)(b[2] + (b[3] << 7));
            break;
        default:
            break;
        }
    } else if (status == 0xFD) {
        uint8_t key = (uint8_t)(b[2] | ((b[1] & 0x10) ? 0x80 : 0));
        uint8_t id = b[3];

        switch (b[0]) {
        case 0:
            FOR_CHANNEL_VOICES(ch, v) {
                if (fd_matches(v, key, id)) {
                    v->expression = scale127(b[4]);
                    volume_ch_update(v);
                }
            }
            break;
        case 1: {
            int8_t a = (int8_t)(b[4] - 0x40);

            if (a < -0x3F) {
                a = -0x3F;
            }
            FOR_CHANNEL_VOICES(ch, v) {
                if (fd_matches(v, key, id)) {
                    pan_apply(v, a);
                }
            }
            break;
        }
        case 2:
            FOR_CHANNEL_VOICES(ch, v) {
                if (fd_matches(v, key, id)) {
                    bend_update(v, (int16_t)((b[4] + (b[5] << 7)) - 0x2000));
                }
            }
            break;
        case 0x10:
            if (b[4] != 0) {
                note_on(env, ch, port, key & 0x7F, b[4], id);
            } else {
                note_off(ch, key & 0x7F, id);
            }
            break;
        default:
            break;
        }
    }
}

/* Data bytes after a status 0x80..0xE0 beyond the first (0x10B1D0) */
static const uint8_t has_second_byte[8] = {0, 1, 1, 1, 0, 0, 1, 0};

/* 0x1014AC: no running status; a message cut off by the end of the data is lost */
static void parse_midi(HSynEnv *env, unsigned port, const uint8_t *p, const uint8_t *end) {
    do {
        uint8_t status = *p++;

        if (!(status & 0x80)) {
            continue;
        }
        if (status < 0xF0) {
            uint8_t d1;
            uint8_t d2 = 0;

            if (p >= end) {
                return;
            }
            d1 = *p++;
            if (has_second_byte[(status - 0x80) >> 4]) {
                if (p >= end) {
                    return;
                }
                d2 = *p++;
            }
            channel_message(env, port, status, d1, d2);
        } else if (status == 0xF0) {
            /* SysEx, up to F7: given to an empty function */
            if (p >= end) {
                return;
            }
            if (*p++ != 0xF7) {
                do {
                    if (p >= end) {
                        return;
                    }
                } while (*p++ != 0xF7);
            }
        } else if (status == 0xF9 || status == 0xFD) {
            const uint8_t *args = p;

            p += status == 0xF9 ? 4 : 6;
            if (p <= end) {
                special_message(env, port, status, args);
            }
        }
    } while (p < end);
}

/* 0x10161C: each port takes all of its stream */
static void parse_streams(CslBuffCtx *buffCtx, int buffNum) {
    unsigned port = 0;
    int i;

    for (i = 1; i < buffNum; i += 2, port++) {
        CslMidiStream *stream = (CslMidiStream *)buffCtx[i - 1].buff;
        HSynEnv *env = (HSynEnv *)buffCtx[i].buff;
        const uint8_t *data = stream->data;
        const uint8_t *end = stream->data + stream->validsize;

        if (!(data < end)) {
            continue;
        }
        stream->validsize = 0;
        if (env->portMode == 0) {
            parse_midi(env, port, data, end);
        } else if (env->portMode == 1) {
            /* 0x102608: sound-effect messages (FE ...), which KF4 never sends */
            LOG_ONCE("MODHSYN: sound-effect message port (portMode 1), not implemented");
        }
    }
}

/* ---- The tick ------------------------------------------------------------------------------------ */
/* 0x103514: key-offs, then the voices whose old sound has died away key on; the number still
   waiting */
static int key_on_pending(void) {
    int waiting = 0;
    int c;

    for (c = 0; c < 2; c++) {
        if (koffMask[c] != 0) {
            sd_set_switch((uint16_t)(SW_KOFF | c), koffMask[c]);
            koffMask[c] = 0;
        }
    }
    for (c = 0; c < 2; c++) {
        HSynLink *l;

        for (l = pendingList[c].prev; l != &pendingList[c]; l = l->prev) {
            Voice *v = VOICE_OF_CORE(l);
            HSynLink *next;

            if ((v->owner == 1 && v->key == 0xFF) || sd_get_param((uint16_t)(voice_id(v) | VP_ENVX)) >= 1000) {
                waiting++;
                continue;
            }
            next = l->next;
            key_on(v);
            unlink_node(l);
            link_front(l, &activeList[c]);
            l = next;
        }
        if (konMask[c] != 0) {
            sd_set_switch((uint16_t)(SW_KON | c), konMask[c]);
            konMask[c] = 0;
        }
    }
    return waiting;
}

/* 0x104A64: frees the finished voices (unless a note-on polled them this tick) and steps the
   others; the number stepped */
static int update_voices(void) {
    int work = 0;
    int c;

    for (c = 0; c < 2; c++) {
        uint32_t endx = sd_get_switch((uint16_t)(SW_ENDX | c));
        HSynLink *l;

        for (l = activeList[c].prev; l != &activeList[c]; l = l->prev) {
            Voice *v = VOICE_OF_CORE(l);
            uint16_t flags = v->flags;

            if (pollPending) {
                uint16_t env = sd_get_param((uint16_t)(voice_id(v) | VP_ENVX));

                if (voice_finished(v, endx, &env)) {
                    HSynLink *next = l->next;

                    voice_free(v);
                    l = next;
                    continue;
                }
                if (voiceStat != NULL) {
                    voiceStat->voice_env[c][v->voice] = env;
                }
            }
            if (v->vagi != NULL) {
                if (flags & VF_PORTA_UP) {
                    v->portaOffset += v->portaRate;
                    if (v->portaOffset >= 0) {
                        v->portaOffset = 0;
                        v->flags &= (uint16_t)~VF_PORTA_UP;
                    }
                } else if (flags & VF_PORTA_DOWN) {
                    v->portaOffset -= v->portaRate;
                    if (v->portaOffset <= 0) {
                        v->portaOffset = 0;
                        v->flags &= (uint16_t)~VF_PORTA_DOWN;
                    }
                }
                lfo_tick(&v->plfo);
                pitch_update(v);
            }
            lfo_tick(&v->alfo);
            volume_update(v);
            work++;
        }
    }
    pollPending = 1;
    return work;
}

/* ---- The exports --------------------------------------------------------------------------------- */
/* 0x101A18: port p is the pair buffCtx[2p] (its stream) and buffCtx[2p + 1] (its state) */
static HSynEnv *port_env(CslCtx *ctx, unsigned int port) {
    CslBuffGrp *grp;

    if (ctx == NULL) {
        return NULL;
    }
    grp = ctx->buffGrp;
    if (port * 2 + 1 >= (uint32_t)grp->buffNum) {
        return NULL;
    }
    return (HSynEnv *)grp->buffCtx[port * 2 + 1].buff;
}

/* 0x100640 */
static void channel_init(HSynChannel *ch, HSynEnv *env) {
    int k;

    for (k = 0; k < 3; k++) {
        list_init(&ch->voices[k]);
    }
    ch->program = NULL;
    ch->env = env;
    ch->bank = NULL;
    ch->portaTime = 0;
    ch->bend = 0;
    ch->exBend = 0xFFFF;
    ch->bankNo = 0;
    ch->volume = 0x80;
    ch->expression = 0x80;
    ch->breath = 0;
    ch->modulation = 0;
    ch->exExpression = 0xFF;
    ch->exPan = 0xFF;
    ch->pan = 0;
    ch->damper = 0;
    ch->portaKey = 0xFF;
    ch->nrpnMsb = 0xFF;
    ch->nrpnLsb = 0xFF;
    ch->nrpnData = 0xFF;
}

/* 0x103260: all voices free, in order (so the first taken is voice 23) */
static void voices_init(void) {
    int c;
    int i;

    for (c = 0; c < 2; c++) {
        list_init(&reservedList[c]);
        list_init(&activeList[c]);
        list_init(&pendingList[c]);
        list_init(&freeList[c]);
    }
    for (c = 0; c < 2; c++) {
        for (i = 0; i < 24; i++) {
            Voice *v = &voices[c][i];

            /* the rest (the LFOs among it) keeps what it had */
            v->exBend = 0xFFFF;
            v->flags = 0;
            v->env = NULL;
            v->ch = NULL;
            v->prog = NULL;
            v->split = NULL;
            v->smpl = NULL;
            v->vagi = NULL;
            v->bdSpu = 0;
            v->envx = 0;
            v->portaOffset = 0;
            v->priority = 0;
            v->core = (uint8_t)c;
            v->voice = (uint8_t)i;
            v->exExpression = 0xFF;
            v->exPan = 0x80;
            v->id = 0xFF;
            v->owner = 0xFF;
            v->pitchOverride = 0;
            link_tail(&v->coreLink, &freeList[c]);
        }
    }
    freeCount[0] = freeCount[1] = 24;
    koffMask[0] = koffMask[1] = 0;
    konMask[0] = konMask[1] = 0;
    nonMask[0] = nonMask[1] = 0;
    vmixl[0] = vmixl[1] = vmixr[0] = vmixr[1] = 0;
    vmixel[0] = vmixel[1] = vmixer[0] = vmixer[1] = 0;
}

/* 0x100050: every port's state is reset (the caller sets its fields afterwards) */
int sceHSyn_Init(CslCtx *ctx, unsigned int interval_us) {
    const char *prev = sd_trace_module("MODHSYN");
    CslBuffGrp *grp;
    int ports = 0;
    int i;

    voiceStat = NULL;
    tickUnit = (uint32_t)(interval_us << 8) / 1000u;
    if (ctx == NULL || ctx->buffGrpNum == 0 || ctx->buffGrp == NULL) {
        sd_trace_module(prev);
        return -1;
    }
    grp = ctx->buffGrp;
    if (grp->buffNum < 2 || grp->buffCtx == NULL) {
        sd_trace_module(prev);
        return -1;
    }
    for (i = 1; i < grp->buffNum; i += 2) {
        HSynEnv *env = (HSynEnv *)grp->buffCtx[i].buff;
        int n;

        /* a missing buffer fails with the earlier ports already reset */
        if (grp->buffCtx[i - 1].buff == NULL || env == NULL) {
            sd_trace_module(prev);
            return -1;
        }
        env->priority = 0;
        env->maxPolyphony = 0x30;
        env->portMode = 0;
        env->waveType = 0;
        env->lfoWaveTbl = NULL;
        env->lfoWaveNum = 0;
        env->velocityMapTbl = NULL;
        env->velocityMapNum = 0;
        env->volume = 0x100;
        env->voices = 0;
        for (n = 0; n < 16; n++) {
            memset(&env->bank[n], 0, sizeof(env->bank[n]));
        }
        for (n = 0; n < 16; n++) {
            channel_init(&env->channel[n], env);
        }
        ports++;
    }
    if (ports == 0) {
        sd_trace_module(prev);
        return -1;
    }
    voices_init();
    reserveMask[0] = reserveMask[1] = 0;
    mono = 0;
    pollPending = 1;
    seVoices = 0;
    midiVoices = 0;
    sd_trace_module(prev);
    return 0;
}

/* 0x1016F8 */
int sceHSyn_ATick(CslCtx *ctx) {
    const char *prev = sd_trace_module("MODHSYN");
    int pending;
    int work;
    int c;

    /* reserved voices' mix bits are whatever the hardware has */
    for (c = 0; c < 2; c++) {
        uint32_t r = reserveMask[c];

        vmixl[c] &= ~r;
        vmixr[c] &= ~r;
        vmixel[c] &= ~r;
        vmixer[c] &= ~r;
        if (r != 0) {
            vmixl[c] |= sd_get_switch((uint16_t)(SW_VMIXL | c)) & r;
            vmixr[c] |= sd_get_switch((uint16_t)(SW_VMIXR | c)) & r;
            vmixel[c] |= sd_get_switch((uint16_t)(SW_VMIXEL | c)) & r;
            vmixer[c] |= sd_get_switch((uint16_t)(SW_VMIXER | c)) & r;
        }
    }
    parse_streams(ctx->buffGrp->buffCtx, ctx->buffGrp->buffNum);
    pending = key_on_pending();
    work = update_voices();
    if (voiceStat != NULL) {
        voiceStat->pendingVoiceCount = pending;
        voiceStat->workVoiceCount = work;
    }
    sd_trace_module(prev);
    return 0;
}

/* "IECS" then the tag reversed, as the PS2 compares two little-endian words */
static int chunk_is(const uint8_t *chunk, const char *tag) {
    static const char scei[4] = {'S', 'C', 'E', 'I'};
    int i;

    for (i = 0; i < 4; i++) {
        if (chunk[i] != (uint8_t)scei[3 - i] || chunk[4 + i] != (uint8_t)tag[3 - i]) {
            return 0;
        }
    }
    return 1;
}

static const uint8_t *hd_chunk(const uint8_t *hd, const uint8_t *head, uint32_t offset) {
    uint32_t o = rd32(head, offset);

    return o != 0xFFFFFFFFu ? hd + o : NULL;
}

/* 0x10035C: the .hd is not copied (it stays where it is), and nothing is checked past the chunks'
   tags; bd_spu or hd 0 unloads */
int sceHSyn_Load(CslCtx *ctx, unsigned int port, uint32_t bd_spu, void *hd, unsigned int bank) {
    const char *prev = sd_trace_module("MODHSYN");
    const uint8_t *h = (const uint8_t *)hd;
    HSynEnv *env;
    HSynBank *slot;

    if (ctx == NULL || bank >= 16 || (env = port_env(ctx, port)) == NULL) {
        sd_trace_module(prev);
        return -1;
    }
    if (env->waveType == 1) {
        LOG_ONCE("MODHSYN: Setb bank (waveType 1) loaded, not implemented");
        bank = 0;
    }
    slot = &env->bank[bank];
    if (bd_spu == 0 || h == NULL) {
        memset(slot, 0, sizeof(*slot));
        sd_trace_module(prev);
        return 0;
    }
    slot->head = h + 0x10;
    slot->prog = hd_chunk(h, slot->head, 0x14);
    slot->sset = hd_chunk(h, slot->head, 0x18);
    slot->smpl = hd_chunk(h, slot->head, 0x1C);
    slot->vagi = hd_chunk(h, slot->head, 0x20);
    slot->setb = h[0x0E] >= 2 ? hd_chunk(h, slot->head, 0x24) : NULL;
    if (!chunk_is(h, "Vers") || !chunk_is(slot->head, "Head") || (slot->prog != NULL && !chunk_is(slot->prog, "Prog")) ||
        (slot->sset != NULL && !chunk_is(slot->sset, "Sset")) || (slot->smpl != NULL && !chunk_is(slot->smpl, "Smpl")) ||
        (slot->vagi != NULL && !chunk_is(slot->vagi, "Vagi")) || (slot->setb != NULL && !chunk_is(slot->setb, "Setb"))) {
        memset(slot, 0, sizeof(*slot));
        sd_trace_module(prev);
        return -1;
    }
    slot->bdSpu = bd_spu;
    sd_trace_module(prev);
    return 0;
}

/* 0x100248: a DMA write to sound memory, waited for; 0 if it moved the size rounded up to 64 */
int sceHSyn_VoiceTrans(short channel, uint32_t iop, uint32_t spu, unsigned int size) {
    const char *prev = sd_trace_module("MODHSYN");
    uint32_t rounded = (size >> 6) << 6;
    int moved;

    if (size & 0x3F) {
        rounded += 0x40;
    }
    sd_voice_trans_status(channel, 1);
    moved = sd_voice_trans(channel, SD_TRANS_WRITE, iop, spu, size);
    sd_voice_trans_status(channel, 1);
    sd_trace_module(prev);
    return (uint32_t)moved != rounded ? -1 : 0;
}

/* 0x101C10: the mode goes through a table (0..10, others 0); bit 8 clears the work area */
int sceHSyn_SetEffectAttr(const HSynEffectAttr *attr) {
    const char *prev = sd_trace_module("MODHSYN");
    spu2_sd_effect_attr sd;
    int mode = (attr->mode & 0xF) <= 10 ? attr->mode & 0xF : 0;

    sd.core = attr->core;
    sd.mode = mode | (attr->mode & 0x100);
    sd.depth_L = attr->depth_L;
    sd.depth_R = attr->depth_R;
    sd.delay = attr->delay;
    sd.feedback = attr->feedback;
    sd_set_effect_attr(attr->core, &sd);
    sd_set_core_attr((uint16_t)((uint16_t)attr->core | CA_EFFECT_ENABLE), mode != 0);
    sd_set_param((uint16_t)((uint16_t)attr->core | SD_PARAM_EVOLL), attr->vol_L);
    sd_set_param((uint16_t)((uint16_t)attr->core | SD_PARAM_EVOLR), attr->vol_R);
    sd_trace_module(prev);
    return 0;
}

/* 0x101A68: taken by the voices as they next update */
int sceHSyn_SetVolume(CslCtx *ctx, unsigned int port, unsigned short volume) {
    const char *prev = sd_trace_module("MODHSYN");
    HSynEnv *env = port_env(ctx, port);

    sd_trace_module(prev);
    if (env == NULL) {
        return -1;
    }
    env->volume = volume;
    return 0;
}

/* 0x101AE4: CC123 on each channel */
int sceHSyn_AllNoteOff(CslCtx *ctx, unsigned int port) {
    const char *prev = sd_trace_module("MODHSYN");
    HSynEnv *env = port_env(ctx, port);
    int n;

    if (env == NULL) {
        sd_trace_module(prev);
        return -1;
    }
    for (n = 0; n < 16; n++) {
        all_notes_off(&env->channel[n]);
    }
    sd_trace_module(prev);
    return 0;
}

/* 0x101BAC: CC121 on each channel */
int sceHSyn_ResetAllControler(CslCtx *ctx, unsigned int port) {
    const char *prev = sd_trace_module("MODHSYN");
    HSynEnv *env = port_env(ctx, port);
    int n;

    if (env == NULL) {
        sd_trace_module(prev);
        return -1;
    }
    for (n = 0; n < 16; n++) {
        reset_controllers(&env->channel[n]);
    }
    sd_trace_module(prev);
    return 0;
}

/* 0x101CF4 */
int sceHSyn_SetVoiceStatBuffer(HSynVoiceStat *buffer) {
    const char *prev = sd_trace_module("MODHSYN");

    voiceStat = buffer;
    sd_trace_module(prev);
    return 0;
}

/* 0x101E4C: per channel, bit 0/1/2: it has held / damper-held / released voices */
int sceHSyn_GetChStat(CslCtx *ctx, unsigned int port, uint8_t *stat) {
    const char *prev = sd_trace_module("MODHSYN");
    HSynEnv *env = port_env(ctx, port);
    int n;
    int k;

    sd_trace_module(prev);
    if (env == NULL) {
        return -1;
    }
    for (n = 0; n < 16; n++) {
        uint8_t bits = 0;

        for (k = 0; k < 3; k++) {
            if (env->channel[n].voices[k].next != &env->channel[n].voices[k]) {
                bits |= (uint8_t)(1 << k);
            }
        }
        stat[n] = bits;
    }
    return 0;
}

/* 0x101EE8: 0 mono (pans centred as voices next pan), 1 stereo */
int sceHSyn_SetOutputMode(int mode) {
    const char *prev = sd_trace_module("MODHSYN");
    int result = 0;

    if (mode == 0) {
        mono = 1;
    } else if (mode == 1) {
        mono = 0;
    } else {
        result = -1;
    }
    sd_trace_module(prev);
    return result;
}

/* 0x102FB4: the status bytes of the port's voices of note id `id` (MIDI ports only, with a status
   buffer), channel by channel, each list from its tail; the count */
int sceHSyn_MSGetVoiceStateByID(CslCtx *ctx, unsigned int port, unsigned char id, unsigned char *out,
                                char count) {
    const char *prev = sd_trace_module("MODHSYN");
    HSynEnv *env = port_env(ctx, port);
    int found = 0;
    int n;
    int k;

    sd_trace_module(prev);
    if (env == NULL || voiceStat == NULL || env->portMode != 0) {
        return -1;
    }
    for (n = 0; n < 16; n++) {
        for (k = 0; k < 3; k++) {
            HSynLink *head = &env->channel[n].voices[k];
            HSynLink *l;

            for (l = head->prev; l != head; l = l->prev) {
                Voice *v = VOICE_OF_CH(l);

                if (v->id == id) {
                    out[found++] = voiceStat->voice_state[v->core][v->voice];
                    if (found >= (signed char)count) {
                        return found;
                    }
                }
            }
        }
    }
    return found;
}
