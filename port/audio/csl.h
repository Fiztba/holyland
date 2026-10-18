/* Sony's CSL (Component Sound Library) on the IOP, as KF4's sound driver uses it: modmidi, the MIDI
   sequencer (port/audio/modmidi.c), and modhsyn, the synthesizer (port/audio/modhsyn.c).

   The names are the public CSL API's (csl.h, modmidi.h, modhsyn.h); the behaviour is that of the
   disc's MODMIDI.IRX (PsIImodmidi 2200) and MODHSYN.IRX (PsIImodhsyn 2240), written from their code.
   A context lists buffers in groups: modmidi's group 0 holds, per port, the sequence (.sq) and the
   port's state, and group 1 the output streams; modhsyn's group 0 holds, per port, the input stream
   and the port's state. Sequence port p writes stream p, which synthesizer port p reads. */
#ifndef PORT_AUDIO_CSL_H
#define PORT_AUDIO_CSL_H

#include <stdint.h>

typedef struct {
    int sema;
    void *buff;
} CslBuffCtx;

typedef struct {
    int buffNum;
    CslBuffCtx *buffCtx;
} CslBuffGrp;

typedef struct {
    int buffGrpNum;
    CslBuffGrp *buffGrp;
    void *conf;
    void *callBack;
    char **extmod;
} CslCtx;

/* A stream of MIDI messages: written by a sequence port (or the driver), read by a synthesizer port,
   which takes all of it each tick. buffsize counts the 8-byte header. */
#define CSL_STREAM_DATA 0x400
typedef struct {
    uint32_t buffsize;
    uint32_t validsize;
    uint8_t data[CSL_STREAM_DATA];
} CslMidiStream;

/* ---- modmidi ---------------------------------------------------------------------------------- */
typedef uint32_t (*MidiChMsgCallBack)(uint32_t msg, void *priv);

/* Loop table entry (the sequence's NRPN loops) */
typedef struct {
    uint8_t id;            /* 0xFF: free */
    uint8_t count;         /* repeats left, 0xFF: not started */
    uint8_t run_status;
    uint8_t no_delta;
    uint32_t time;
    const uint8_t *ptr;
} MidiLoop;

/* What a channel last set, sent again when play starts (the "chase"); 0xFF/0xFFFF: never set */
typedef struct {
    uint8_t prog, cc0, cc1, cc2, cc5, cc7, cc10, cc11, cc64, cc65;
    uint16_t bend;
} MidiChase;

#define MIDI_ST_READY 0x001
#define MIDI_ST_INPLAY 0x002
#define MIDI_ST_DATAEND 0x004
#define MIDI_ST_NOLOOP 0x008

/* A sequence port's state (sceMidiEnv) */
typedef struct {
    uint32_t songNum;
    uint32_t midiNum;
    uint32_t position;
    uint32_t status;                   /* MIDI_ST_* */
    uint16_t outPort[16];              /* per MIDI channel, the output streams (bit n: stream n) */
    uint16_t excOutPort;
    MidiChMsgCallBack chMsgCallBack;   /* sees (and may change) each channel message */
    void *chMsgCallBackPrivateData;
    void *metaMsgCallBack;
    void *metaMsgCallBackPrivateData;
    void *excMsgCallBack;
    void *excMsgCallBackPrivateData;
    void *repeatCallBack;
    void *repeatCallBackPrivateData;
    /* the sequencer's own (sceMidiEnv.system) */
    const uint8_t *sequ_chunk;
    const uint8_t *midi_chunk;
    const uint8_t *song_chunk;
    const uint8_t *comp_table;
    const uint8_t *data_start;
    const uint8_t *ptr;
    uint32_t next_time;
    uint32_t acc;
    uint32_t tick_len;
    uint32_t division;
    uint32_t tempo;
    uint32_t rel_tempo;
    uint8_t run_status;
    uint8_t no_delta;
    uint8_t master_vol;
    uint8_t nrpn_msb;
    uint8_t data_msb;
    uint16_t used_ch;
    uint8_t cc7[16];
    uint8_t ch_vol[16];
    MidiChase chase[16];
    MidiLoop loop[8];
} MidiEnv;

int sceMidi_Init(CslCtx *ctx, unsigned int interval_us);
int sceMidi_ATick(CslCtx *ctx);
int sceMidi_Load(CslCtx *ctx, unsigned int port);
int sceMidi_SelectMidi(CslCtx *ctx, unsigned int port, unsigned int midi_num);
int sceMidi_MidiPlaySwitch(CslCtx *ctx, unsigned int port, int sw);
int sceMidi_MidiSetLocation(CslCtx *ctx, unsigned int port, unsigned int tick);
unsigned char sceMidi_MidiGetAbsoluteTempo(CslCtx *ctx, unsigned int port);
int sceMidi_MidiSetRelativeTempo(CslCtx *ctx, unsigned int port, unsigned short rel);
unsigned short sceMidi_MidiGetRelativeTempo(CslCtx *ctx, unsigned int port);

/* ---- modhsyn ---------------------------------------------------------------------------------- */
typedef struct HSynEnv HSynEnv;

/* A link of the synthesizer's intrusive voice lists */
typedef struct HSynLink {
    struct HSynLink *next;
    struct HSynLink *prev;
} HSynLink;

/* A bank slot (0x1C bytes on the PS2): the chunks of a loaded .hd, which stays where it was loaded,
   and the sound-memory address of its samples */
typedef struct HSynBank {
    const uint8_t *head;
    const uint8_t *prog;
    const uint8_t *sset;
    const uint8_t *smpl;
    const uint8_t *vagi;
    const uint8_t *setb;
    uint32_t bdSpu;
} HSynBank;

/* A MIDI channel of a synthesizer port (0x38 bytes on the PS2) */
typedef struct HSynChannel {
    HSynLink voices[3];        /* its voices: key held, held by the damper, released */
    const uint8_t *program;    /* the Prog entry (NULL: notes play nothing) */
    HSynEnv *env;
    HSynBank *bank;            /* the program's bank */
    int16_t portaTime;         /* CC5 * 20 (ms) */
    int16_t bend;              /* -0x2000..0x1FFF */
    uint16_t exBend;           /* F9 pitch bend for the next note-on, 0xFFFF: none */
    uint8_t bankNo;            /* CC0 */
    uint8_t volume;            /* CC7, CC11, CC2, CC1, each scaled by 128/127 */
    uint8_t expression;
    uint8_t breath;
    uint8_t modulation;
    uint8_t exExpression;      /* F9 expression and pan for the next note-on, 0xFF: none */
    uint8_t exPan;
    int8_t pan;                /* CC10 - 0x40, at least -63 */
    uint8_t damper;            /* CC64 */
    uint8_t portaKey;          /* the portamento's source key: 0xFF off, 0x80 on with no key yet */
    uint8_t portaControl;      /* CC84 */
    uint8_t nrpnMsb;           /* 0xFF: not set */
    uint8_t nrpnLsb;
    uint8_t nrpnData;
} HSynChannel;

/* A synthesizer port's state (sceHSynEnv, 0x558 bytes on the PS2): the first fields are the
   caller's, set after sceHSyn_Init (which resets them) */
struct HSynEnv {
    uint8_t priority;                  /* added to each sample's priority */
    uint8_t maxPolyphony;
    uint8_t portMode;                  /* 0 MIDI messages, 1 sound-effect messages */
    uint8_t waveType;                  /* 0 Prog/Sset/Smpl/Vagi banks, 1 Setb banks */
    void *lfoWaveTbl;
    uint32_t lfoWaveNum;
    void *velocityMapTbl;
    uint32_t velocityMapNum;
    /* the synthesizer's own (sceHSynEnv.system) */
    uint16_t volume;                   /* 0x100: unity */
    uint8_t voices;                    /* voices the port holds */
    HSynChannel channel[16];
    HSynBank bank[16];
};

/* sceHSyn_EffectAttr: libsd's sceSdEffectAttr then the effect return volumes */
typedef struct {
    int core;
    int mode;
    short depth_L;
    short depth_R;
    int delay;
    int feedback;
    uint16_t vol_L;
    uint16_t vol_R;
} HSynEffectAttr;

/* The voice status buffer (sceHSyn_VoiceStat), 0x98 bytes: the voices waiting to key on and the
   voices updated at the last tick, then per voice (core, voice) a state byte (0x90 | port at
   note-on, bit 5 at key-on, bits 4-5 at release, bits 4-6 cleared when the voice is freed) and its
   envelope (ENVX, 0x7FFF for the first ticks after key-on) */
typedef struct {
    int32_t pendingVoiceCount;
    int32_t workVoiceCount;
    uint8_t voice_state[2][24];
    uint16_t voice_env[2][24];
} HSynVoiceStat;

int sceHSyn_Init(CslCtx *ctx, unsigned int interval_us);
int sceHSyn_ATick(CslCtx *ctx);
/* hd: the bank header's host address (NULL unloads); bd_spu: the bank's samples in sound memory */
int sceHSyn_Load(CslCtx *ctx, unsigned int port, uint32_t bd_spu, void *hd, unsigned int bank);
int sceHSyn_VoiceTrans(short channel, uint32_t iop, uint32_t spu, unsigned int size);
int sceHSyn_SetEffectAttr(const HSynEffectAttr *attr);
int sceHSyn_SetVolume(CslCtx *ctx, unsigned int port, unsigned short volume);
int sceHSyn_AllNoteOff(CslCtx *ctx, unsigned int port);
int sceHSyn_ResetAllControler(CslCtx *ctx, unsigned int port);
int sceHSyn_SetVoiceStatBuffer(HSynVoiceStat *buffer);
int sceHSyn_GetChStat(CslCtx *ctx, unsigned int port, uint8_t *stat);
int sceHSyn_SetOutputMode(int mode);
int sceHSyn_MSGetVoiceStateByID(CslCtx *ctx, unsigned int port, unsigned char id, unsigned char *out,
                                char count);

#endif
