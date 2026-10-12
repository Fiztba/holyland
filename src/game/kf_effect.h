/* Effect actors: short-lived effects that move a model and end in another effect. */
#ifndef KF_EFFECT_H
#define KF_EFFECT_H

#include "common.h"
#include "kf_model.h"

/* the effect's definition, EffectActor.def */
typedef struct {
    Vector4 keys[9];                /* 0x00 keyframe offsets (func_001AEC48) */
    u16 unk90;                      /* 0x90 frame the second segment starts at */
    u16 unk92;                      /* 0x92 frame the third segment starts at */
    char pad94[0x08];
    u8 curve[3];                    /* 0x9C curve shape per segment */
    char pad9F[0x07];
    u8 unkA6;                       /* 0xA6 */
    char padA7[0x01];
    f32 start_x;                    /* 0xA8 centre of the box a scattering effect aims into */
    f32 start_y;                    /* 0xAC */
    f32 start_z;                    /* 0xB0 */
    f32 start_spread_x;             /* 0xB4 half the aim box size */
    f32 start_spread_y;             /* 0xB8 */
    f32 start_spread_z;             /* 0xBC */
    f32 origin_x;                   /* 0xC0 centre of the box a scattering effect starts in */
    f32 origin_y;                   /* 0xC4 */
    f32 origin_z;                   /* 0xC8 */
    f32 spread_x;                   /* 0xCC half the box size */
    f32 spread_y;                   /* 0xD0 */
    f32 spread_z;                   /* 0xD4 */
    f32 speed;                      /* 0xD8 */
    f32 unkDC;                      /* 0xDC */
    f32 unkE0;                      /* 0xE0 */
    f32 unkE4;                      /* 0xE4 */
    f32 unkE8;                      /* 0xE8 */
    u16 radius_half;                /* 0xEC */
    u16 interval;                   /* 0xEE frames between collision checks */
    u16 unkF0;                      /* 0xF0 effect ids passed to func_001A2838 */
    u16 unkF2;                      /* 0xF2 */
    char padF4[0x0C];
    u16 unk100;                     /* 0x100 */
    u16 restart_frame;              /* 0x102 frame to resume from after the last spawn */
    f32 gravity;                    /* 0x104 */
    u16 max_frames;                 /* 0x108 0: no limit */
    char pad10A[0x01];
    u8 unk10B;                      /* 0x10B 0xFF: never ends on a hit */
    char pad10C[0x0C];
    u16 end_effect;                 /* 0x118 spawned when the effect ends */
    char pad11A[0x02];
    f32 unk11C;                     /* 0x11C */
} EffectDef;

/* EffectActor+0x10, which the update functions keep a pointer to. Each kind of
   effect uses the bytes after the velocity its own way; the *Motion types below
   are those views, reached by a cast (a union changes gcc's scheduling). */
typedef struct {
    Vector4 velocity;               /* 0x00 (EffectActor+0x10) */
    char unk10[0x20];               /* 0x10 per-kind state */
} EffectMotion;

/* EffectDef.max_frames and the bytes after it read as one 64-bit flag word
   (reached by cast, like EFFECT_FLAG_BITS) */
typedef struct {
    char pad00[0x108];
    s64 flags;                      /* 0x108 */
} EffectDefFlags;

/* ObjectSlot+0x20, whose low word func_001A2F20 reads as a pointer to the effect
   definition while the same 8 bytes are ObjectSlot.flags (reached by cast, not a union) */
typedef struct {
    char pad00[0x20];
    EffectDefFlags *def;            /* 0x20 */
} ObjectSlotDefView;

#define OBJECT_SLOT_DEF(slot) (((ObjectSlotDefView *)(slot))->def)
#define EFFECT_DEF_FLAGS(def) ((EffectDefFlags *)(def))
#define EFFECT_DEF_FOLLOWS_HEADING 0x80000  /* moves along its model's heading, its scale growing */

/* EffectMotion as func_001A50E8 and func_001A4F78 use it: a countdown and a state byte after the velocity */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 radius_half;                /* 0x10 (func_001A4F78) */
    u8 count;                       /* 0x14 (func_001A4F78) frames left */
    s8 state;                       /* 0x15 0: not yet checked, 1: reached its target */
    char pad16[0x1A];
} SeekingMotion;

/* EffectMotion as func_001A4E38 uses it: a flag byte after the velocity */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x04];
    s8 ended;                       /* 0x14 set once the end effects are spawned */
    char pad15[0x1B];
} EndedMotion;

/* EffectMotion as func_001AABA0 fills it: the start point and aim point it picks, then the velocity
   (so unlike the other kinds the first Vector4 is not a velocity) */
typedef struct {
    Vector4 start;                  /* 0x00 */
    Vector4 aim;                    /* 0x10 */
    Vector4 velocity;               /* 0x20 */
} ScatterMotion;

/* EffectMotion as func_001A4CE8 uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    u16 end_frame;                  /* 0x10 */
    char pad12[0x1E];
} ProjectileMotion;

/* EffectMotion as func_001A96B0 (falls under gravity) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    int mode;                       /* 0x10 3 or 5: spawn effect 0x15 on landing */
    u16 end_frame;                  /* 0x14 */
    s8 spawned;                     /* 0x16 a follow-up effect has been spawned */
    char unk17[0x19];               /* 0x17 passed by address */
} FallingMotion;

/* EffectMotion as func_001ACFC0 uses it: a scale and a countdown byte */
typedef struct {
    f32 scale;                      /* 0x00 */
    s8 countdown;                   /* 0x04 frames the model is held still */
    char pad05[0x2B];
} ShockMotion;

/* EffectMotion as func_001AA590 uses it: spawns effect_id at end_frame or on a hit */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x10];
    u16 effect_id;                  /* 0x20 */
    u16 end_frame;                  /* 0x22 */
    s8 ended;                       /* 0x24 */
    char pad25[0x0B];
} DelayedMotion;

/* EffectMotion as func_001A7148 reads it from the effect that picked the targets: an entity and a distance per slot */
typedef struct {
    struct Entity *entity[4];       /* 0x00 */
    f32 distance[4];                /* 0x10 */
    char pad20[0x10];
} PickedMotion;

/* EffectMotion as func_001A98D0 (checks for hits every interval frames) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x04];
    int countdown;                  /* 0x14 frames to the next check */
    char unk18[0x18];               /* 0x18 passed by address */
} EmitterMotion;

/* EffectMotion as func_001A9F98 uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x15];
    s8 unk25;                       /* 0x25 set to 1 after a step */
    s8 unk26;                       /* 0x26 the frame counter wrapped into the def range */
    char pad27[0x09];
} StepMotion;

/* EffectMotion as func_001AC1F8 (scales its model) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x10];
    f32 scale_per_mille;            /* 0x20 */
    char pad24[0x0C];
} ScalerMotion;

/* EffectMotion as func_001AB188 uses it: a countdown byte and a flag byte after the velocity */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    u8 count;                       /* 0x10 */
    s8 flag;                        /* 0x11 cleared at the end of each step */
    char pad12[0x1E];
} CountedMotion;
/* EffectMotion as func_001A52B8 uses it: a flag byte right after the velocity */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    s8 unk10;                       /* 0x10 cleared when the model is lifted */
    char pad11[0x1F];
} LiftedMotion;

/* EffectMotion as func_001A6BF8 fills it: the point to aim at */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 target_x;                   /* 0x10 */
    f32 target_y;                   /* 0x14 */
    f32 target_z;                   /* 0x18 */
    char pad1C[0x09];
    s8 aimed;                       /* 0x25 set once the target is chosen */
    char pad26[0x0A];
} AimedMotion;

/* EffectMotion as func_001A8ED0 uses it: moves by the velocity, ends after a countdown */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    Vector4 end_position;           /* 0x10 where the end effect appears */
    u16 end_frame;                  /* 0x20 */
    s8 countdown;                   /* 0x22 */
    char pad23[0x0D];
} CountdownMotion;

/* EffectMotion as func_001AA940 uses it: the distances the effect fades in over */
typedef struct {
    f32 range[2];                   /* 0x00 fade-in distance, full-size distance */
    char pad08[0x28];
} FadingMotion;

/* EffectMotion as func_001AA330 and func_001A5D20 (turn toward a point) use it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char unk10[0x10];               /* 0x10 passed by address */
    s16 unk20;                      /* 0x20 */
    s16 unk22;                      /* 0x22 */
    u8 unk24;                       /* 0x24 */
    u8 unk25;                       /* 0x25 */
    u8 unk26;                       /* 0x26 */
    char pad27[0x09];
} TurningMotion;

/* EffectMotion as func_001AA230 uses it: the definition's collision radius and
   interval copied over the start of the velocity */
typedef struct {
    u16 radius_half;                /* 0x00 */
    u16 interval;                   /* 0x02 */
    char pad04[0x2C];
} PulseMotion;

/* EffectMotion as func_001A4570 (spawns an effect every interval) uses it */
typedef struct {
    u16 count;                      /* 0x00 effects spawned so far */
    u16 effect_id;                  /* 0x02 */
    u16 repeat_limit;               /* 0x04 */
    u16 interval_base;              /* 0x06 */
    u16 next_frame;                 /* 0x08 */
    u16 interval_range;             /* 0x0A */
    char pad0C[0x24];
} SpawnerMotion;

/* EffectMotion as func_001ACE80 (stays attached to another model) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    ModelInstance **anchor;         /* 0x10 the model it is attached to */
    char unk14[0x1C];               /* 0x14 passed by address */
} AttachedMotion;

/* EffectMotion as func_001AAF58 (falls once its frame passes fall_frame) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x08];
    s32 fall_frame;                 /* 0x18 gravity applies once the frame passes this */
    char unk1C[0x14];               /* 0x1C passed by address */
} GravityMotion;

/* EffectMotion as func_001A4128 (sits on another entity) uses it */
typedef struct {
    s32 radius_half;                /* 0x00 */
    s8 unk04;                       /* 0x04 */
    char pad05[0x1B];
} SnappedMotion;

/* EffectMotion as func_001A6748 (rests on the ground) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 range;                      /* 0x10 */
    s32 unk14;                      /* 0x14 */
    u8 landed;                      /* 0x18 */
    char pad19[0x17];
} LandingMotion;

/* EffectMotion as func_001AD278 (follows EffectActor.target) uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 speed;                      /* 0x10 the target's velocity.x */
    s32 unk14;                      /* 0x14 */
    f32 unk18;                      /* 0x18 */
    u8 unk1C;                       /* 0x1C */
    char pad1D[0x03];
    s8 unk20;                       /* 0x20 */
    char pad21[0x0F];
} FollowerMotion;

/* EffectMotion as func_001ABBE0 uses it */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x0C];
    char unk1C;                     /* 0x1C */
    char pad1D[0x13];
} BurstMotion;

/* EffectMotion as func_001A3F20 uses it: falls under gravity and scales its step */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 fall_speed;                 /* 0x10 */
    f32 scale;                      /* 0x14 */
    char pad18[0x04];
    char unk1C[0x14];               /* 0x1C passed by address */
} ScaledMotion;

/* EffectMotion as func_001AE060 and func_001AE730 set it up: an end position and two floats */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    Vector4 end_position;           /* 0x10 */
    f32 unk20;                      /* 0x20 */
    f32 unk24;                      /* 0x24 */
    char pad28[0x08];
} DriftMotion;

/* EffectMotion as func_001ACD18 uses it: a flag byte at the very start */
typedef struct {
    u8 grounded;                    /* 0x00 set when the model is dropped onto the entity */
    char pad01[0x2F];
} GroundedMotion;

/* EffectMotion as func_001A8FF0 (aims at a point) uses it */
typedef struct {
    char pad00[0x24];
    s16 heading;                    /* 0x24 */
    char pad26[0x0A];
} HeadingMotion;

/* EffectMotion as func_001A7AC8 uses it */
typedef struct {
    char pad00[0x22];
    u8 unk22;                       /* 0x22 */
    char pad23[0x0D];
} ArmedMotion;

/* EffectMotion as func_001AB870 uses it: the lifetime in frames */
typedef struct {
    s32 lifetime;                   /* 0x00 */
    char pad04[0x2C];
} LifetimeMotion;

/* EffectMotion as func_001A6A20 uses it: the float at 0x10 and a flag byte at 0x14 */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 unk10;                      /* 0x10 */
    u8 unk14;                       /* 0x14 */
    char pad15[0x0B];
} ReseedMotion;

/* EffectMotion as func_001A88A8 uses it: a u16 it tests, and sets on other effects */
typedef struct {
    char pad00[0x26];
    u16 unk26;                      /* 0x26 (EffectActor+0x36) */
    char pad28[0x08];
} MarkedMotion;

/* EffectMotion as func_001AD558 uses it: spawns the end effect at end_frame */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    Vector4 end_position;           /* 0x10 where the end effect appears */
    s32 end_frame;                  /* 0x20 */
    s8 ended;                       /* 0x24 also the start of the block passed by address */
    char pad25[0x0B];
} EndingMotion;

/* EffectMotion as func_001AA2D0 uses it: the frames [start_frame, end_frame) it acts in */
typedef struct {
    u16 start_frame;                /* 0x00 */
    u16 end_frame;                  /* 0x02 */
    char pad04[0x2C];
} FrameWindowMotion;

/* EffectMotion as func_001A72F0 uses it: scales the model, spawns the end effect */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    Vector4 end_position;           /* 0x10 where the end effect appears */
    f32 scale_per_mille;            /* 0x20 */
    s8 ended;                       /* 0x24 */
    char pad25[0x0B];
} ScalingEndMotion;

/* EffectMotion as func_001ADCA8 uses it: the frame the effect lands at */
typedef struct {
    char pad00[0x14];
    u16 landing_frame;              /* 0x14 */
    u8 unk16;                       /* 0x16 */
    char pad17[0x19];
} LandingFrameMotion;

/* EffectMotion as func_001AC8E8 uses it: the frames its two phases end at */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    char pad10[0x10];
    u16 move_end_frame;             /* 0x20 */
    u16 turn_end_frame;             /* 0x22 */
    char pad24[0x0C];
} PhasedMotion;

/* EffectMotion as func_001A7880 sets it up: the velocity, two random offsets and a scale */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    f32 offset_x;                   /* 0x10 */
    f32 offset_y;                   /* 0x14 */
    char pad18[0x04];
    f32 scale;                      /* 0x1C */
    s16 unk20;                      /* 0x20 */
    char pad22;
    u8 unk23;                       /* 0x23 */
    u8 unk24;                       /* 0x24 */
    char pad25[0x0B];
} SprayMotion;

/* EffectMotion as func_001A9790 uses it: the spawn count and phase of a ripple */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    s32 spawn_index;                /* 0x10 */
    s32 phase;                      /* 0x14 */
    u8 flag;                        /* 0x18 */
    char pad19[0x17];
} RippleMotion;

/* EffectMotion as func_001AB9C0 fills it: the spawn index and a scale after the velocity */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    s32 spawn_index;                /* 0x10 */
    f32 scale;                      /* 0x14 */
    u8 flag;                        /* 0x18 */
    char pad19[0x17];
} SpawnedMotion;

/* EffectMotion as func_001A73B0 (homes on a target) sets it up */
typedef struct {
    Vector4 velocity;               /* 0x00 */
    Vector4 aim;                    /* 0x10 */
    s32 timer;                      /* 0x20 */
    u16 flags;                      /* 0x24 */
    char pad26[0x0A];
} HomingMotion;

/* EffectMotion as func_001ADF40 uses it: the heading in degrees, and as a word */
typedef struct {
    s32 heading_word;               /* 0x00 the heading, zero-extended */
    u16 heading;                    /* 0x04 degrees */
    char pad06[0x2A];
} HeadingWordMotion;

typedef struct EffectActor {
    char pad00[0x0C];
    f32 scale;                      /* 0x0C */
    EffectMotion motion;            /* 0x10 */
    s64 flags;                      /* 0x40 */
    void *owner;                    /* 0x48 */
    u16 frame;                      /* 0x4C */
    u16 effect_param;               /* 0x4E */
    ModelInstance *model;           /* 0x50 */
    EffectDef *def;                 /* 0x54 */
    char pad58[0x04];
    int effect_arg;                 /* 0x5C */
    struct EffectActor *target;     /* 0x60 */
    void *start_position;           /* 0x64 */
    void *start_rotation;           /* 0x68 */
    int unk6C;                      /* 0x6C */
    Vector4 vector;                 /* 0x70 */
    char pad80[0x20];
} EffectActor;

/* EffectActor.flags read as two words: the low flag bits and, from 0x44, an index
   func_001AD7A0 hands to func_0019B1E8 (reached by cast, like ENTITY_FLAG_BITS) */
typedef struct {
    u32 low;                        /* 0x40 */
    s16 unk44;                      /* 0x44 */
    u16 unk46;                      /* 0x46 */
} EffectFlagWords;

#define EFFECT_FLAG_WORDS(actor) ((EffectFlagWords *)&(actor)->flags)

/* EffectActor.flags as the bit-fields the game sets (see ENTITY_FLAG_BITS) */
typedef struct {
    u64 pad0 : 57;
    u64 bit57 : 1;                  /* set when its definition asks to be tracked */
    u64 pad58 : 1;
    u8 bit59 : 1;
    u64 pad60 : 4;
} EffectFlagBits;

#define EFFECT_FLAG_BITS(actor) ((EffectFlagBits *)&(actor)->flags)

/* D_00502260[effect id]: an effect's definition-table entry (0x120 bytes) */
typedef struct {
    char pad00[0x108];
    u64 pad108 : 16;
    u8 track : 1;                   /* 0x108 bit 16: the actor is tracked by its object slot */
    u64 pad109 : 47;
    char pad110[0x10];
} EffectTableEntry;

/* EffectActor.effect_arg is the Entity an effect follows, for the kinds that follow one */
#define EFFECT_SOURCE(actor) ((struct Entity *)(actor)->effect_arg)

/* a request to spawn an effect at a position (func_00214660) */
typedef struct {
    s32 pending;                    /* 0x00 cleared once the effect is spawned */
    char pad04[0x04];
    char kind;                      /* 0x08 */
    char pad09[0x07];
    u8 unk10;                       /* 0x10 */
    char pad11[0x07];
    u8 unk18;                       /* 0x18 */
    char pad19[0x07];
    f32 unk20;                      /* 0x20 */
    char pad24[0x04];
    f32 pos_x;                      /* 0x28 */
    char pad2C[0x04];
    f32 pos_y;                      /* 0x30 */
    char pad34[0x04];
    f32 pos_z;                      /* 0x38 */
} SpawnRequest;

/* a spawn request with a float at 0x18 and the position at 0x20 (func_002145F0, func_00214580) */
typedef struct {
    s32 pending;                    /* 0x00 cleared once the effect is spawned */
    char pad04[0x04];
    u8 kind;                        /* 0x08 */
    char pad09[0x07];
    u8 unk10;                       /* 0x10 */
    char pad11[0x07];
    f32 unk18;                      /* 0x18 */
    char pad1C[0x04];
    f32 pos_x;                      /* 0x20 */
    char pad24[0x04];
    f32 pos_y;                      /* 0x28 */
    char pad2C[0x04];
    f32 pos_z;                      /* 0x30 */
} SpawnRequestWide;

/* a task that feeds the cosine of an angle to a command (func_00214DC0) */
typedef struct {
    s32 active;                     /* 0x00 cleared after one step */
    char pad04[0x04];
    f32 angle;                      /* 0x08 radians */
    char command[4];                /* 0x0C the command func_00211D90 fills in */
} AngleTask;

/* D_00402DA8[]: a table entry of 0x110 bytes, indexed by effect id (func_001B4F30) */
typedef struct {
    char pad00[0x100];
    u16 unk100;                     /* 0x100 */
    s8 unk102;                      /* 0x102 nonzero: the effect cannot be started */
    char pad103[0x0D];
} FxTableEntry;

#endif
