/* Questions the effect code asks: whether an entity can be hit, whether a point is near one, distances, and
   when a moving effect reaches what it is heading for. */
#include "entity/entity.h"
#include "model/model.h"
#include "effect/effect.h"
#include "math/vu0.h"
#include "collision/collision.h"
#include <math.h>

float Effect_Distance(void *, void *);
void Collision_CastRayAtMap(f32, void *, void *);

extern Entity *active_entities_head;

/* Nonzero if the entity cannot be targeted: not live, dying, or flagged out. */
int Effect_IsEntityUntargetable(Entity *entity) {
    int excluded;

    if (!ENTITY_FLAG_BITS(entity)->bit1 || ENTITY_FLAG_BITS(entity)->bits2 > 0) {
        return TRUE;
    }
    if (ENTITY_FLAG_BITS(entity)->bit16 == 1) {
        return TRUE;
    }
    excluded = FALSE;
    if (ENTITY_FLAG_BITS(entity)->bit21) {
        excluded = TRUE;
    }
    return excluded;
}

/* Nonzero if the point is within radius (plus the entity's own radius) of the middle of any live entity. */
int Effect_IsNearAnyEntity(float radius, void *point) {
    float position[4] __attribute__((aligned(16)));
    Entity *entity;

    for (entity = active_entities_head; entity != NULL; entity = entity->next) {
        if (((int)(entity->flags >> 1) & 1) == 0) continue;
        if (((int)(entity->flags >> 2) & 3) > 0) continue;
        Vector_Copy(position, &ENTITY_MODEL(entity)->position);
        position[1] -= entity->height * 0.5f;
        if (Effect_Distance(point, position) <= radius + entity->radius) return TRUE;
    }
    return FALSE;
}

/* Distance between two points. */
float Effect_Distance(void *from, void *target) {
    f32 *a = from;
    f32 *b = target;
    f32 dx = b[0] - a[0];
    f32 dy = b[1] - a[1];
    f32 dz = b[2] - a[2];
    f32 sum;

    /* vsub, vmul, then z + x, then + y, and the square root (VU0) */
    sum = dz * dz + dx * dx;
    sum = sum + dy * dy;
    return sqrtf(sum);
}

// Horizontal (XZ-plane) distance between origin and target.
float Effect_DistanceXZ(float *origin, float *target) {
    Vector4 out;
    Vector4 target_at_origin_height;

    target_at_origin_height.x = target[0];
    target_at_origin_height.y = origin[1];
    target_at_origin_height.z = target[2];
    target_at_origin_height.w = 0;
    {
        f32 dx = target_at_origin_height.x - origin[0];
        f32 dy = target_at_origin_height.y - origin[1];
        f32 dz = target_at_origin_height.z - origin[2];
        f32 sum;

        /* vsub, vmul, then z + x, then + y, and the square root (VU0) */
        sum = dz * dz + dx * dx;
        sum = sum + dy * dy;
        out.x = sqrtf(sum);
    }
    return out.x;
}

/* the inline twin of Math_AbsFloat */
static inline f32 absolute_value(f32 value) {
    if (value < 0.0f) {
        return -value;
    }
    return value;
}

/* Casts the effect's path forward; stores where it lands in hit_point and returns the
   number of frames to get within radius of it, or -1 if it hits nothing. */
s16 Effect_FramesToImpact(float radius, void *effect, void *motion, void *hit_point) {
    EffectActor *actor = effect;
    f32 distance;
    f32 stack_reserve[4]; // match: unused, keeps the retail stack frame size

    Collision_CastRayAtMap(actor->def->keyframe_end, &actor->model->position, motion);
    if (COLLISION_RESULT->hit != 0) {
        distance = Effect_Distance(COLLISION_RESULT->point, &actor->model->position);
        Vector_Copy(hit_point, COLLISION_RESULT->point);
        return absolute_value((distance - radius) / actor->def->speed);
    }
    return -1;
}
