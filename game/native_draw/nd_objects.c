/* nd_objects.c: PC-only. Native draw (--native-draw objects; docs/port/NATIVE_DRAW.md, NATIVE_DRAW_TASKS.md
   P2-1) of the map objects: Model_DrawPosedWithAlpha, the draw_parts of every map object whose
   definition's bit8 is 0 (MapObject_SetUp ...), and of nothing else. It is called both straight from
   MapObject_DrawAll (solid objects) and from DrawList_Draw (translucent and fading ones, put in the draw
   list by MapObject_DrawAll), so the call site is the head of Model_DrawPosedWithAlpha itself (model.patch):
   one hook for both ways in.

   What the function queues, and so what this queues in its place, in the chain's order:
   - VU1's fixed block (the two light matrices, the fog, FOGCOL, the point lights), as Model_DrawPosed's
     but without the colours at 0x64-0x66 even with model flag 0x800 (it allocates them and never writes
     them: a model with that flag is left to the PS2 path here, see below);
   - per part (not switched off by bit40, not scaled to nothing), GS registers, then the part's draw:
     - not fading (fade_enabled 0): PRMODECONT 1 (the PRIM's own attributes: context 1), TEST_1, ZBUF_1
       twice (written, then masked unless the part has bit41);
     - fading: PRMODECONT 0 and PRMODE 0x278 (Gouraud, textured, fogged, blended, context 2), TEX1_2 and
       TEX0_2 from the part's first data block, ALPHA_2 with the block's A, B and D and C = FIX, the fade
       as FIX (fade_alpha), TEST_2, and ZBUF_2 masked while fade_alpha is below 0x80;
   - after the parts: PRMODECONT 1 and ZBUF_2 twice.
   The batches' own A+D packets (TEX0_1 ...) and PRIM go through the native GS as VU1's kicks would
   (gsn_native_draw.inc), so with PRMODECONT 0 the GS draws them in context 2 with the fade, as on the
   PS2 path.

   The draws' identity: kind RD_KIND_MAP_OBJECT, the model (its EE word), the part. */
#include "common.h"
#include "model/model.h"
#include "map/map.h"
#include "entity/entity.h"
#include "gfx/gs.h"
#include "port/render.h"

#include <string.h>

void Packet_BeginGsRegisters(void);
void Packet_AddGsRegister(int reg, s64 value);
void Packet_EndGsRegisters(void);
void Matrix_Multiply(void *dst, void *matrix, void *src);
void ModelMatrix_BuildDrawMatrices(ModelInstance *, sptr, sptr, void *, void *);
void Model_DrawPosedWithAlpha(ModelInstance *model);

/* native_draw.c's helpers (its "For the other adapter files") */
int NativeDraw_PartMeshes(ModelPart *part, RdMesh *meshes);
u32 NativeDraw_CurrentCamera(void);
void NativeDraw_QueueMark(u32 command, u64 bits);
void NativeDraw_QueueFixedBlock(ModelInstance *model, const void *matrix_a, const void *matrix_b);

extern f32 world_view_matrix[16];

#define ND_MAX_MESHES 2048 /* (ModelPart.data_count is 11 bits; as native_draw.c) */
/* the meshes of all a model's parts, kept from the check that every block parses to the draws, so each
   block is hashed once a draw, not twice (P2-6: the hash is most of what a cached draw costs); a model
   with more blocks than this has its parts' meshes found again (and hashed again) part by part */
#define ND_MODEL_MESHES 8192

/* set while the PS2 path's packets are queued for --native-draw-compare: Model_DrawPosedWithAlpha, whose
   head calls in here, then draws the PS2 way */
static int queuing_ps2_path;

/* Model_DrawPosedWithAlpha(model), natively: 1 if drawn (or nothing to draw), 0 if the caller is to draw
   it the PS2 way (a block the core cannot draw, model flag 0x800, or the compare mode's own call). */
int NativeDraw_PosedWithAlpha(ModelInstance *model) {
    static RdMesh meshes[ND_MODEL_MESHES];
    static RdMesh scratch[ND_MAX_MESHES];
    f32 (*matrix_a)[4] = MODEL_DRAW_MATRIX_A;
    f32 (*matrix_b)[4] = MODEL_DRAW_MATRIX_B;
    ModelPart *part;
    RdDraw d;
    EEWord owner = 0;
    int kept = 1; /* every part's meshes are in meshes[], one part after another */
    u32 used = 0;
    int i;

    if (queuing_ps2_path) {
        return 0;
    }
    if (model->parts == NULL || model->part_count == 0) {
        return 1; /* (the PS2 path queues nothing either) */
    }
    /* Flag 0x800 makes Model_DrawPosedWithAlpha allocate 0x21 quadwords for its fixed block and write 0x1C:
       the chain then carries 5 quadwords nobody wrote. Never seen on a map object; left to the PS2 path
       rather than guessed at. */
    if (model->model_flags & 0x800) {
        return 0;
    }
    /* every part's blocks first: a model is drawn one way or the other, whole */
    part = model->parts;
    for (i = 0; i < model->part_count; i++, part++) {
        RdMesh *into = kept && used + part->data_count <= ND_MODEL_MESHES ? meshes + used : scratch;

        if (into == scratch) {
            kept = 0;
        }
        if (!NativeDraw_PartMeshes(part, into)) {
            return 0;
        }
        used += part->data_count;
    }
    ModelMatrix_BuildDrawMatrices(model, model->light_block, model->light_block + 0x40, matrix_a, matrix_b);
    if (!port_native_draw_compare()) {
        /* (without the parts: VU1 is left as the PS2 path leaves it; native_draw.c's queue_fixed_block) */
        NativeDraw_QueueFixedBlock(model, matrix_a, matrix_b);
    }
    ee_word_try(model, &owner);
    memset(&d, 0, sizeof(d));
    d.kind = RD_KIND_MAP_OBJECT;
    d.id[0] = (u32)owner;
    d.program = 0x000;
    d.camera = NativeDraw_CurrentCamera();
    memcpy(d.light_dir, matrix_a, sizeof(d.light_dir));
    memcpy(d.light_col, matrix_b, sizeof(d.light_col));
    memcpy(d.fog, model->fog_block, sizeof(d.fog));
    d.fogcol = (u64)((MapFogBlockValues *)model->fog_block)->unk10;
    d.point_count = model->hit_count;
    for (i = 0; i < model->hit_count && i < 4; i++) {
        memcpy(d.point_pos[i], &linked_point_pool[model->hit_handles[i]].position, 16);
        d.point_pos[i][3] = linked_point_pool[model->hit_handles[i]].progress_scale;
        memcpy(d.point_col[i], &linked_point_pool[model->hit_handles[i]].target, 16);
    }
    part = model->parts;
    used = 0;
    for (i = 0; i < model->part_count; i++, part++) {
        f32 matrix[4][4] __attribute__((aligned(16)));
        ModelPartData *data;
        RdMesh *these = kept ? meshes + used : scratch;
        u64 value;

        used += part->data_count;
        if (MODEL_PART_FLAG_WORD(part) & MODEL_PART_BIT40) {
            continue;
        }
        if (part->scale[0] == 0.0f && part->scale[1] == 0.0f && part->scale[2] == 0.0f) {
            continue;
        }
        if (!kept) {
            NativeDraw_PartMeshes(part, scratch); /* (found again in the cache) */
        }
        Matrix_Multiply(matrix, model->model_matrix, part);
        memcpy(d.model, matrix, sizeof(d.model));
        d.id[1] = (u32)i;
        value = rd_draw(&d, these, part->data_count);
        Packet_BeginGsRegisters();
        if (model->fade_enabled == 0) {
            Packet_AddGsRegister(0x1A, 1);
            Packet_AddGsRegister(GS_REG_TEST_1, 0x5000D);
            Packet_AddGsRegister(GS_REG_ZBUF_1, 0xA000118);
            if (MODEL_PART_FLAG_WORD(part) & MODEL_PART_BIT41) {
                Packet_AddGsRegister(GS_REG_ZBUF_1, 0xA000118);
            } else {
                Packet_AddGsRegister(GS_REG_ZBUF_1, 0x10A000118LL);
            }
        } else {
            GsAlphaReg *blend;

            Packet_AddGsRegister(0x1A, 0);
            Packet_AddGsRegister(0x1B, 0x278);
            data = EE_PTR(ModelPartData *, part->data_addr);
            blend = (GsAlphaReg *)&data->blend;
            Packet_AddGsRegister(0x15, *(s64 *)&data->texture_word);
            Packet_AddGsRegister(0x07, data->tex0);
            Packet_AddGsRegister(GS_REG_ALPHA_2, (s32)blend->f.a_source | ((s32)blend->f.b_source << 2) | 0x20 |
                                                     ((s32)blend->f.d_source << 6) | ((s64)(s16)model->fade_alpha << 32));
            Packet_AddGsRegister(GS_REG_TEST_2, 0x5000D);
            if ((s16)model->fade_alpha >= 0x80) {
                Packet_AddGsRegister(GS_REG_ZBUF_2, 0xA000118);
            } else {
                Packet_AddGsRegister(GS_REG_ZBUF_2, 0x10A000118LL);
            }
        }
        if (value != 0) {
            Packet_AddGsRegister(PORT_GS_REG_NATIVE_DRAW, (s64)value);
        }
        Packet_EndGsRegisters();
    }
    Packet_BeginGsRegisters();
    Packet_AddGsRegister(0x1A, 1);
    Packet_AddGsRegister(GS_REG_ZBUF_2, 0x10A000118LL);
    Packet_AddGsRegister(GS_REG_ZBUF_2, 0x10A000118LL);
    Packet_EndGsRegisters();
    if (port_native_draw_compare()) {
        NativeDraw_QueueMark(RD_ND_ALT_BEGIN, 0);
        queuing_ps2_path = 1;
        Model_DrawPosedWithAlpha(model);
        queuing_ps2_path = 0;
        NativeDraw_QueueMark(RD_ND_ALT_END, 0);
    }
    return 1;
}
