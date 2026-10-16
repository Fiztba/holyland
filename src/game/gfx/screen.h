/* Screen effects: fades, alpha-blended overlays and the screen wobble table. */
#ifndef KF_SCREEN_H
#define KF_SCREEN_H

#include "common.h"
#include "model/model.h"

/* the model ModelTexture_SendForModel draws; the older name for ModelInstance */
typedef ModelInstance OverlaySprite;

/* one element PoolSlot_Draw draws (PoolSlot_DrawAll, PoolSlot_DrawAllBut15 and PoolSlot_DrawFirst20But15 run it
   over an array of them): a pool slot (PoolSlot, core/game.h), whose first 0x40 bytes are a matrix loaded whole.
   On the PC it is the PoolSlot itself, so that the arrays step by the slot's PC size. */
typedef struct PoolSlot RenderSlot;

/* PoolSlot_Draw's second argument: eight bytes passed in one register, only spilled */
typedef struct {
    s32 unk00;                      /* 0x00 */
    s32 unk04;                      /* 0x04 */
} RenderSlotArg;

/* screen_fade: the screen fade */
typedef struct {
    Vector4 position;               /* 0x00 where the player goes in the new map */
    s32 kind;                       /* 0x10 1: MapChange_StartToPosition started it, 2: MapChange_StartPassage */
    s32 unk14;                      /* 0x14 */
    s32 step;                       /* 0x18 index into screen_fade_steps, 0: no map change running */
    s32 map;                        /* 0x1C the map to change to */
    s32 file_handle;                /* 0x20 */
    s32 draw_step;                  /* 0x24 index into screen_fade_draw_steps */
    int timer;                      /* 0x28 frames left */
} ScreenFade;

/* one screen wobble oscillator (gfx/wobble.c keeps 16); its sine wave moves the screen */
typedef struct {
    f32 amp_x;                      /* 0x00 */
    f32 amp_y;                      /* 0x04 */
    s16 x;                          /* 0x08 amp_x * sin(phase), 4.12 fixed point */
    s16 y;                          /* 0x0A */
    f32 phase;                      /* 0x0C */
} WaveOscillator;

/* what held_item_model_ptr points at: a view of ModelInstance (model/model.h) and of the ModelChannel its
   level_tables points at, with the padding worked out from those structs (their pointers move the fields on
   the PC) */
typedef struct {
    char pad00[offsetof(ModelChannel, param)];
    u16 unk82;                      /* 0x82 ModelChannel.param */
} DisplaySettings;

typedef struct {
    char pad00[offsetof(ModelInstance, level_tables)];
    DisplaySettings *settings;      /* 0xFC ModelInstance.level_tables */
} DisplayContext;

_Static_assert(offsetof(DisplaySettings, unk82) == offsetof(ModelChannel, param), "DisplaySettings is not ModelChannel");
_Static_assert(offsetof(DisplayContext, settings) == offsetof(ModelInstance, level_tables), "DisplayContext is not ModelInstance");

/* the nodes Map_InsertRecordBySize sorts into the lists record_boxes and backdrop_record_boxes by box area */
typedef struct AreaNode {
    char pad00[0x3C];
    s32 flags;                      /* 0x3C bit 25 skips insertion, bit 31 picks the second list */
    f32 left;                       /* 0x40 */
    f32 unk44;                      /* 0x44 the box's lowest y */
    f32 top;                        /* 0x48 */
    char pad4C[0x04];
    f32 right;                      /* 0x50 */
    f32 unk54;                      /* 0x54 the box's highest y */
    f32 bottom;                     /* 0x58 */
    struct AreaNode *next;          /* 0x5C */
} AreaNode;

/* an AreaNode with the doubleword its flags are the high half of; bit 27 is the map slot it belongs to */
typedef struct {
    char pad00[0x38];
    u64 unk38;                      /* 0x38 */
} AreaNodeWords;

#define AREA_NODE_WORD(node) (((AreaNodeWords *)(node))->unk38)

#endif
