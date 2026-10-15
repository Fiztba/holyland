#include "kf_map.h"
#include "kf_collision.h"
#include "kf_player.h"
#include "kf_entity.h"
#include "kf_effect.h"
#include "kf_vu0.h"

/* moves the player out of the body just hit: to `push` from it, but by no more than 50 a frame */
#define PUSH_PLAYER_OUT()                                                                          \
    Vector_Scale(offset, COLLISION_RESULT->normal, push);                                       \
    Vector_Add(offset, COLLISION_RESULT, offset);                                               \
    Vector_Subtract(offset, offset, &D_00413F80);                                               \
    length = sqrtf(offset[0] * offset[0] + offset[2] * offset[2]);                                 \
    if (50.0f < length) {                                                                          \
        offset[1] = 0.0f;                                                                          \
        Vector_Scale(offset, offset, 50.0f / length);                                           \
        Vector_Add(&D_00413F80, &D_00413F80, offset);                                           \
    } else {                                                                                       \
        D_00413F80.position.x = COLLISION_RESULT->point[0] + COLLISION_RESULT->normal[0] * push;   \
        D_00413F80.position.z = COLLISION_RESULT->point[2] + COLLISION_RESULT->normal[2] * push;   \
    }

int func_001C2D08(void *);
void func_001C0A88(void *, void *, void *, void *);
void func_001C0C50(void *, void *, void *, void *, f32);
u32 func_001124E0(int, u32, u32);
u32 func_001125A8(u32, u32, int);
void func_001152B8(int);
float sqrtf(float);
void Collision_CylinderCylinder(void *, f32, f32, void *, f32, f32);
void func_001C2758(f32 *, s32, f32, f32);
s32 func_001C2A38(f32 *, s32, f32, f32);
void func_001BB360(void);

extern PlayerState D_00413F80;
extern PlayerState D_00414280;      /* the player as of the frame before */
extern int D_00446770;
extern MapSlot D_0038CD20[2];
extern MapSlotMemory D_004321F0[2];
extern f32 D_00446968[];
extern f32 D_0044696C[];    /* the largest float; an array so that it is not read through $gp */
extern Entity *D_004467E0;
extern u32 D_00446ED4;
extern u8 spell_levels[6][5];
extern FxTableEntry D_00402DA8[];

/* Tests the player against the collision meshes of the records around him: what he touches, the walls that
   push him back, the water above him and the floor he stands on. Returns TRUE when a floor was found. */
int func_001BDDC0(void) {
    // match: the scratchpad points through pointers (retail forms each address anew), and the order of these
    // declarations decides which stack slot each value that does not fit a register gets
    Vector4 *wall_point = &COLLISION_SCRATCH->unk90;
    Vector4 *floor_point = &COLLISION_SCRATCH->point;
    Vector4 raised_point;
    s32 step_bit;
    s32 map;
    s32 record_count;
    s32 found;
    u8 stepping;
    s32 floor_map;
    f32 floor_height;
    f32 reach;
    MapSlot *slot;
    MapRecordBounds *record;
    s32 i;
    ModelInstance *model;
    CollisionMesh *mesh;
    CollisionStrip *strip;
    CollisionStripPoint *strip_point;
    s32 strips_left;
    s32 points_left;
    u32 surface;
    f32 drop;

    step_bit = (s32)(D_00413F80.unk1C8 >> 33) & 1;
    stepping = step_bit; // match: tested as the bit, kept as a byte
    if (step_bit) {
        if (D_00413F80.unk1D0 <= D_00413F80.unk1D4) {
            D_00413F80.position.y -= D_00413F80.unk1D0;
            D_00413F80.unk1C8 &= ~(0x8000L << 18);
            D_00413F80.unk1D4 = 0.0f;
            D_00413F80.unk1D0 = 0.0f;
        } else {
            D_00413F80.unk1D0 -= D_00413F80.unk1D4;
            D_00413F80.position.y -= D_00413F80.unk1D4;
        }
    }
    floor_height = D_00446968[0];
    Vector_Copy(wall_point, &D_00413F80.position);
    found = FALSE;
    wall_point->y -= 1.0f;
    Vector_Copy(floor_point, &D_00413F80.position);
    floor_point->y -= 75.0f;
    reach = D_00413F80.unk1AC + 75.0f;
    Vector_Copy(&raised_point, &D_00413F80.position);
    floor_map = D_00446770;
    D_00413F80.unk211 = D_00413F80.unk210 = 6;
    D_00413F80.unk214 = 0.0f;
    raised_point.y += 3.0f; // match: the raised point is never used
    for (map = 0; map < 2; map++) {
        slot = D_00446770 == map ? &D_0038CD20[0] : &D_0038CD20[1];
        if (slot->unk00 != 0) {
            record_count = slot->header->record_count;
            model = (ModelInstance *)D_004321F0[D_00446770 == map ? 0 : 1].unk34;
            record = (MapRecordBounds *)slot->records;
            for (i = 0; i < record_count; i++, record++, model++) {
                if (record->unk32 == 0xFFFF) {
                    continue;
                }
                if (record->unk3C & 0x40000000) {
                    continue;
                }
                if (D_00413F80.position.x < record->lowest[0] - 100.0f
                    || record->highest[0] + 100.0f < D_00413F80.position.x
                    || D_00413F80.position.z < record->lowest[2] - 100.0f
                    || record->highest[2] + 100.0f < D_00413F80.position.z) {
                    continue;
                }
                mesh = (CollisionMesh *)model->strips;
                ((void (*)(void *))func_001C2D08)(model); // match: called as if it returned nothing
                if (D_00413F80.unk1E0 == 0) {
                    strips_left = mesh->unk40;
                    strip = mesh->unk28;
                    while (--strips_left != -1) {
                        strip_point = strip->points;
                        points_left = strip->point_count - 2;
                        do {
                            func_001C0A88(strip_point[0].position, strip_point[1].position, strip_point[2].position,
                                          &D_00413F80.position);
                            if (COLLISION_RESULT->hit != 0 && D_00413F80.position.y - 230.0f <= COLLISION_RESULT->point[1]
                                && COLLISION_RESULT->point[1] <= D_00413F80.position.y) {
                                surface = func_001125A8(strip_point->unk04, strip_point->unk08, strip_point->unk0C);
                                if (surface != 0) {
                                    D_00413F80.unk1E4 = surface;
                                    D_00413F80.unk1E0 = 1;
                                } else {
                                    D_00413F80.unk1EC = 1;
                                }
                            }
                            strip_point++;
                        } while (--points_left != 0);
                        strip = (CollisionStrip *)&strip_point[2];
                    }
                }
                strips_left = mesh->unk38;
                strip = mesh->unk18;
                while (--strips_left != -1) {
                    strip_point = strip->points;
                    points_left = strip->point_count - 2;
                    do {
                        Collision_CapsuleTriangle((f32 *)strip_point[0].position, (f32 *)strip_point[1].position,
                                      (f32 *)strip_point[2].position, (f32 *)&COLLISION_SCRATCH->unk90, 100.0f, 230.0f);
                        if (COLLISION_RESULT->hit != 0) {
                            COLLISION_SCRATCH->push.x = COLLISION_SCRATCH->unk90.x - COLLISION_RESULT->point[0];
                            COLLISION_SCRATCH->push.y = 0.0f;
                            COLLISION_SCRATCH->push.z = COLLISION_SCRATCH->unk90.z - COLLISION_RESULT->point[2];
                            Vector_Normalize(&COLLISION_SCRATCH->push, &COLLISION_SCRATCH->push);
                            Vector_Scale(&COLLISION_SCRATCH->push, &COLLISION_SCRATCH->push, 100.0f);
                            COLLISION_SCRATCH->unk90.x = COLLISION_RESULT->point[0] + COLLISION_SCRATCH->push.x;
                            COLLISION_SCRATCH->unk90.z = COLLISION_RESULT->point[2] + COLLISION_SCRATCH->push.z;
                        }
                        strip_point++;
                    } while (--points_left != 0);
                    strip = (CollisionStrip *)&strip_point[2];
                }
                if (map == 0) {
                    strips_left = mesh->unk42;
                    strip = mesh->unk2C;
                    while (--strips_left != -1) {
                        strip_point = strip->points;
                        points_left = strip->point_count - 2;
                        do {
                            func_001C0A88(strip_point[0].position, strip_point[1].position, strip_point[2].position,
                                          &D_00413F80.position);
                            if (COLLISION_RESULT->hit != 0
                                && COLLISION_RESULT->point[1] <= D_00413F80.position.y + D_00413F80.unk1AC
                                && (!((s32)(D_00413F80.unk1C8 >> 38) & 1) || COLLISION_RESULT->point[1] < D_00413F80.unk1DC)) {
                                D_00413F80.unk1DC = COLLISION_RESULT->point[1];
                                D_00413F80.unk1C8 |= 0x8000L << 23;
                                if (COLLISION_RESULT->point[1] <= D_00413F80.position.y - 220.0f) {
                                    D_00413F80.unk1C8 |= 0x8000L << 24;
                                } else {
                                    D_00413F80.unk1C8 &= ~(0x8000L << 24);
                                }
                            }
                            strip_point++;
                        } while (--points_left != 0);
                        strip = (CollisionStrip *)&strip_point[2];
                    }
                }
                if (stepping) {
                    continue;
                }
                if (D_00413F80.unk208 != 0
                    && (MAP_OBJECT_DEF_FLAGS(((MapObject *)D_00413F80.unk208)->unk04) & MAP_OBJECT_DEF_CARRIES)) {
                    continue;
                }
                strips_left = mesh->floor_strip_count;
                strip = mesh->floor_strips;
                while (--strips_left != -1) {
                    strip_point = strip->points;
                    points_left = strip->point_count - 2;
                    do {
                        func_001C0C50(strip_point[0].position, strip_point[1].position, strip_point[2].position,
                                      &COLLISION_SCRATCH->point, reach);
                        if (COLLISION_RESULT->hit != 0 && COLLISION_RESULT->point[1] < floor_height) {
                            found = TRUE;
                            floor_height = COLLISION_RESULT->point[1];
                            D_00413F80.unk250 = record->unk38;
                            D_00413F80.map_index = i;
                            D_00413F80.unk210 = func_001124E0(strip_point->unk04, strip_point->unk08, strip_point->unk0C);
                            floor_map = D_00446770 != map;
                        }
                        strip_point++;
                    } while (--points_left != 0);
                    strip = (CollisionStrip *)&strip_point[2];
                }
            }
        }
    }
    D_00413F80.position.x = wall_point->x;
    D_00413F80.position.z = wall_point->z;
    if (!stepping) {
        if (found) {
            drop = D_00413F80.position.y - floor_height;
            if (drop < 16.666666f) {
                D_00413F80.position.y = floor_height;
            } else {
                D_00413F80.unk1C8 |= 0x8000L << 18;
                D_00413F80.unk1D4 = 16.666666f;
                D_00413F80.position.y -= 16.666666f;
                D_00413F80.unk1D0 = drop - 16.666666f + 5.0f;
            }
            D_00413F80.unk1AC = 0.0f;
        } else if (D_00413F80.unk1A8 == 0) {
            D_00413F80.position.y += D_00413F80.unk1AC;
        } else {
            D_00413F80.position.y += D_00413F80.unk1AC * (f32)(60 - D_00413F80.unk1A8) / 60.0f;
        }
    }
    if (((s32)(D_00413F80.unk1C8 >> 33) & 1) || found) {
        D_00413F80.unk1C8 &= ~(0x8000L << 19);
    }
    if (!((s32)(D_00414280.unk1C8 >> 38) & 1) && ((s32)(D_00413F80.unk1C8 >> 38) & 1)) {
        D_00413F80.unk1AC = 0.0f;
        D_00413F80.unk1C8 &= ~(0x8000L << 19);
    }
    if (D_00414280.unk1E4 != 4 && D_00413F80.unk1E4 == 4) {
        D_00413F80.unk1AC = 0.0f;
        D_00413F80.unk1C8 &= ~(0x8000L << 19);
    }
    if (D_00413F80.unk1E0 == 0) {
        D_00413F80.unk1E8 = 0;
    } else if (D_00414280.unk1E0 != 0 && D_00414280.unk1E4 == D_00413F80.unk1E4) {
        D_00413F80.unk1E8++;
    } else {
        D_00413F80.unk1E8 = 0;
    }
    func_001152B8(floor_map);
    return found;
}

/* Keeps the player 100 units away from the wall triangles (CollisionMesh.unk18) of the records around him:
   each one that is hit pushes him straight out, level with the ground. */
void func_001BE760(void) {
    Vector4 *point = &COLLISION_SCRATCH->point;
    MapSlot *slot;
    MapRecordBounds *record;
    CollisionMesh *mesh;
    CollisionStrip *strip;
    CollisionStripPoint *strip_point;
    s32 map;
    s32 record_count;
    s32 i;
    s32 strips_left;
    s32 points_left;

    Vector_Copy(&COLLISION_SCRATCH->point, &D_00413F80.position);
    point->y -= 1.0f;
    for (map = 0; map < 2; map++) {
        slot = D_00446770 == map ? &D_0038CD20[0] : &D_0038CD20[1];
        if (slot->unk00 != 0) {
            record_count = slot->header->record_count;
            record = (MapRecordBounds *)slot->records;
            for (i = 0; i < record_count; i++, record++) {
                if (record->unk32 == 0xFFFF) {
                    continue;
                }
                if (record->unk3C & 0x40000000) {
                    continue;
                }
                if (D_00413F80.position.x < record->lowest[0] - 100.0f
                    || record->highest[0] + 100.0f < D_00413F80.position.x
                    || D_00413F80.position.z < record->lowest[2] - 100.0f
                    || record->highest[2] + 100.0f < D_00413F80.position.z) {
                    continue;
                }
                func_001C2D08(record->model);
                mesh = (CollisionMesh *)record->model->strips;
                strips_left = mesh->unk38;
                strip = mesh->unk18;
                while (--strips_left != -1) {
                    strip_point = strip->points;
                    points_left = strip->point_count - 2;
                    do {
                        Collision_CapsuleTriangle((f32 *)strip_point[0].position, (f32 *)strip_point[1].position,
                                      (f32 *)strip_point[2].position, (f32 *)&COLLISION_SCRATCH->point, 100.0f, 230.0f);
                        if (COLLISION_RESULT->hit != 0) {
                            COLLISION_SCRATCH->push.x = COLLISION_SCRATCH->point.x - COLLISION_RESULT->point[0];
                            COLLISION_SCRATCH->push.y = 0.0f;
                            COLLISION_SCRATCH->push.z = COLLISION_SCRATCH->point.z - COLLISION_RESULT->point[2];
                            Vector_Normalize(&COLLISION_SCRATCH->push, &COLLISION_SCRATCH->push);
                            Vector_Scale(&COLLISION_SCRATCH->push, &COLLISION_SCRATCH->push, 100.0f);
                            COLLISION_SCRATCH->point.x = COLLISION_RESULT->point[0] + COLLISION_SCRATCH->push.x;
                            COLLISION_SCRATCH->point.z = COLLISION_RESULT->point[2] + COLLISION_SCRATCH->push.z;
                        }
                        strip_point++;
                    } while (--points_left != 0);
                    strip = (CollisionStrip *)&strip_point[2];
                }
            }
        }
    }
    D_00413F80.position.x = point->x;
    D_00413F80.position.z = point->z;
}

/* Finds the lowest floor triangle within 10 units of the player in the records around him and notes what it is
   made of in PlayerState.unk210. */
void func_001BEA50(void) {
    f32 lowest;
    MapSlot *slot;
    MapRecordBounds *record;
    CollisionMesh *mesh;
    CollisionStrip *strip;
    CollisionStripPoint *strip_point;
    s32 map;
    s32 record_count;
    s32 i;
    s32 strips_left;
    s32 points_left;

    lowest = D_0044696C[0];
    Vector_Copy(&COLLISION_SCRATCH->point, &D_00413F80.position);
    for (map = 0; map < 2; map++) {
        slot = D_00446770 == map ? &D_0038CD20[0] : &D_0038CD20[1];
        if (slot->unk00 != 0) {
            record_count = slot->header->record_count;
            record = (MapRecordBounds *)slot->records;
            for (i = 0; i < record_count; i++, record++) {
                if (record->unk32 == 0xFFFF) {
                    continue;
                }
                if (record->unk3C & 0x40000000) {
                    continue;
                }
                if (D_00413F80.position.x < record->lowest[0] - 100.0f
                    || record->highest[0] + 100.0f < D_00413F80.position.x
                    || D_00413F80.position.z < record->lowest[2] - 100.0f
                    || record->highest[2] + 100.0f < D_00413F80.position.z) {
                    continue;
                }
                func_001C2D08(record->model);
                mesh = (CollisionMesh *)record->model->strips;
                strips_left = mesh->floor_strip_count;
                strip = mesh->floor_strips;
                while (--strips_left != -1) {
                    strip_point = strip->points;
                    points_left = strip->point_count - 2;
                    do {
                        func_001C0C50(strip_point[0].position, strip_point[1].position, strip_point[2].position,
                                      &COLLISION_SCRATCH->point, 10.0f);
                        if (COLLISION_RESULT->hit != 0 && COLLISION_RESULT->point[1] < lowest) {
                            lowest = COLLISION_RESULT->point[1];
                            D_00413F80.unk210 = func_001124E0(strip_point->unk04, strip_point->unk08, strip_point->unk0C);
                        }
                        strip_point++;
                    } while (--points_left != 0);
                    strip = (CollisionStrip *)&strip_point[2];
                }
            }
        }
    }
}

/* Collides the player with the entities. With `climb` set the player is first put on top of any
   entity low enough to step onto (a step higher than 16.67 is taken over several frames); then
   the player is pushed out of every entity touched. Four kinds are made of up to four bodies. */
void func_001BECA8(int climb) {
    f32 old_position[4];
    f32 position[4];
    f32 offset[4];
    s32 stepped = FALSE;
    Entity *entity;
    EntityBodyPoint *body;
    void *point;
    f32 reach;
    f32 push;
    f32 radius;
    f32 height;
    f32 top;
    f32 rise;
    f32 step;
    f32 length;
    s32 i;

    D_00413F80.unk204 = 0;
    if (climb) {
        Vector_Copy(old_position, &D_00413F80);
        D_00413F80.position.y += 1.0f;
        reach = D_00413F80.unk1AC + 75.0f;
        if (!((s32)(D_00413F80.unk1C8 >> 33) & 1)) {
            for (entity = D_004467E0; entity != NULL; entity = entity->next) {
                if (!((s32)(entity->flags >> 1) & 1)) {
                    continue;
                }
                if (((s32)(entity->flags >> 2) & 3) >= 2) {
                    continue;
                }
                if ((s32)(entity->flags >> 27) & 1) {
                    continue;
                }
                switch (entity->spawn_info->type_id) {
                case 0x11:
                case 0x14:
                case 0x97:
                case 0x9B:
                    continue;
                }
                Vector_Copy(position, entity->model->position);
                radius = entity->radius;
                if (!(((EntityParamsWord *)entity->params)->unk00 & 0x8000000)) {
                    height = entity->height;
                } else {
                    height = radius + radius;
                    position[1] += radius;
                }
                if (radius + 100.0f < entity->distance_to_player) {
                    continue;
                }
                top = position[1] - height;
                if (D_00413F80.position.y < top) {
                    continue;
                }
                if (position[1] < D_00413F80.position.y - 230.0f) {
                    continue;
                }
                if ((((EntityParamsWord *)entity->params)->unk00 & 0x6000000) == 0x4000000) {
                    continue;
                }
                if (entity->spawn_info->type_id == 0x50) {
                    continue;
                }
                if (entity->spawn_info->type_id == 0x9F) {
                    continue;
                }
                if (reach < D_00413F80.position.y - top) {
                    continue;
                }
                D_00413F80.position.y = top;
                stepped = TRUE;
                D_00413F80.unk204 = (s32)entity;
            }
        }
        D_00413F80.position.y -= 1.0f;
        if (stepped) {
            step = 16.666666f;
            rise = old_position[1] - D_00413F80.position.y;
            if (step < rise) {
                D_00413F80.position.y = old_position[1] - step;
                D_00413F80.unk1D0 = rise - step + 5.0f;
                D_00413F80.unk1D4 = step;
                D_00413F80.unk1C8 |= 1L << 33;
            }
            D_00413F80.unk1AC = 0.0f;
            D_00413F80.unk1C8 &= ~(1L << 34);
        }
    }
    for (entity = D_004467E0; entity != NULL; entity = entity->next) {
        if (!((s32)(entity->flags >> 1) & 1)) {
            continue;
        }
        if (((s32)(entity->flags >> 2) & 3) >= 2) {
            continue;
        }
        if ((s32)(entity->flags >> 27) & 1) {
            continue;
        }
        if ((((EntityParamsWord *)entity->params)->unk00 & 0x6000000) == 0x4000000) {
            continue;
        }
        switch (entity->spawn_info->type_id) {
        case 0x11:
        case 0x14:
        case 0x97:
        case 0x9B:
            ENTITY_MODEL(entity)->unk1B4(ENTITY_MODEL(entity));
            body = entity->params->unk20;
            for (i = 0; i < 4; i++, body++) {
                COLLISION_RESULT->hit = 0;
                if (!(body->unk00.flags & ENTITY_BODY_USED)) {
                    continue;
                }
                if (entity->spawn_info->type_id == 0x9B && i != 3) {
                    continue;
                }
                point = (void *)ENTITY_MODEL(entity)->get_point(ENTITY_MODEL(entity), body->unk00.point);
                Vector_Copy(position, point);
                if (!(body->unk00.flags & ENTITY_BODY_ROUND)) {
                    radius = body->unk04 * ENTITY_MODEL(entity)->unkE0;
                    height = body->unk06 * ENTITY_MODEL(entity)->unkE4;
                } else {
                    radius = body->unk04 * ENTITY_MODEL(entity)->unkE0;
                    position[1] += radius;
                    height = radius + radius;
                }
                push = 100.0f;
                Collision_CylinderCylinder(&D_00413F80, push, 230.0f, position, radius, height);
                if (COLLISION_RESULT->hit != 0) {
                    PUSH_PLAYER_OUT();
                }
            }
            break;
        default:
            push = 100.0f;
            Collision_CylinderCylinder(&D_00413F80, push, 230.0f, entity->model->position, entity->radius, entity->height);
            if (COLLISION_RESULT->hit != 0) {
                PUSH_PLAYER_OUT();
            }
            break;
        }
    }
}

/* Collides the player with the map objects. With `climb` set the player is first tried a little
   higher, so a low object is stepped onto; a step higher than 16.67 is taken over several frames
   (status bit 33). An object touched for the first time starts the timed flag its definition
   names. */
void func_001BF350(s32 climb) {
    f32 old_position[4];
    s32 hit = FALSE;
    f32 reach;
    f32 rise;
    f32 step;

    D_00413F80.unk208 = 0;
    D_00413F80.unk20C = 0;
    if (climb) {
        Vector_Copy(old_position, &D_00413F80);
        D_00413F80.position.y += 1.0f;
        reach = D_00413F80.unk1AC + 75.0f;
        if (D_00414280.unk208 != 0) {
            D_00413F80.position.y += 30.0f;
            reach += 30.0f;
        }
        if (!((s32)(D_00413F80.unk1C8 >> 33) & 1)) {
            hit = func_001C2A38((f32 *)&D_00413F80, 1, reach, 100.0f);
        }
        if (hit) {
            step = 16.666666f;
            rise = old_position[1] - D_00413F80.position.y;
            if (step < rise && D_00414280.unk208 != D_00413F80.unk208) {
                D_00413F80.position.y = old_position[1] - step;
                D_00413F80.unk1D0 = rise - step + 5.0f;
                D_00413F80.unk1D4 = step;
                D_00413F80.unk1C8 |= 1L << 33;
            }
            D_00413F80.unk1AC = 0.0f;
            D_00413F80.unk1C8 &= ~(1L << 34);
        } else {
            Vector_Copy(&D_00413F80, old_position);
        }
    }
    if (!((s32)(D_00413F80.unk1C8 >> 33) & 1)) {
        func_001C2758((f32 *)&D_00413F80, 0, 230.0f, 100.0f);
    }
    if (D_00413F80.unk20C != 0 && D_00413F80.unk20C != D_00414280.unk20C) {
        switch ((s16)((MapObject *)D_00413F80.unk20C)->unk08->unk52) {
        case 1:
            D_00413F80.timed_flags[0] = 1800;
            D_00413F80.unk2C4 = D_00446ED4;
            break;
        case 2:
            D_00413F80.timed_flags[1] = 150;
            D_00413F80.unk2C8 = D_00446ED4;
            break;
        case 3:
            D_00413F80.timed_flags[2] = 3600;
            D_00413F80.unk2CC = D_00446ED4;
            break;
        case 4:
            D_00413F80.timed_flags[3] = 5400;
            D_00413F80.unk2D0 = D_00446ED4;
            break;
        case 5:
            D_00413F80.timed_flags[4] = 1800;
            D_00413F80.unk2D4 = D_00446ED4;
            break;
        case 6:
            D_00413F80.timed_flags[5] = 18000;
            D_00413F80.unk2D8 = D_00446ED4;
            break;
        }
        func_001BB360();
    }
    if (D_00413F80.unk208 != 0 && ((MapObject *)D_00413F80.unk208)->unk08->unk30 == 0x165) {
        D_00413F80.unk210 = 2;
    }
}

int func_001BF5F8(void) {
    int i;
    int j;

    for (i = 0; i < 6; i++) {
        for (j = 0; j < 5; j++) {
            if (spell_levels[i][j] != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int func_001BF650(void) {
    int i;
    int j;

    for (i = 0; i < 6; i++) {
        for (j = 0; j < 5; j++) {
            if (spell_levels[i][j] != 0) {
                if (D_00402DA8[15 * i + 3 * j + 1].unk102 == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

int func_001BF6E8(void) {
    int i;
    int j;
    int k;
    FxTableEntry *effect;

    for (i = 0; i < 6; i++) {
        for (j = 0; j < 5; j++) {
            if (spell_levels[i][j] != 0) {
                k = 15 * i + 3 * j + 1;
                effect = &D_00402DA8[k];
                if (effect->unk102 == 1) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
