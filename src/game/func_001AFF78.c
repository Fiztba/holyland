#include "kf_entity.h"
#include "kf_effect.h"

extern Entity *D_004467E0;

int func_001B0040(Entity *);
float func_001B0178(void *, void *);

/* Finds the entity nearest to the effect's target position that is within the effect's check range. */
Entity *func_001AFF78(EffectActor *effect, void *position) {
    float reserved[8] __attribute__((aligned(16))); /* unused, but retail reserves the 0x20 bytes */
    Entity *nearest;
    Entity *entity;
    float distance;
    float best_distance;

    nearest = NULL;
    entity = D_004467E0;
    best_distance = 65535.0f;
    while (entity != NULL) {
        if (func_001B0040(entity) == 0) {
            distance = func_001B0178(&ENTITY_MODEL(entity)->position, position);
            if (!((float)effect->def->interval < distance) && distance < best_distance) {
                best_distance = distance;
                nearest = entity;
            }
        }
        entity = entity->next;
    }
    return nearest;
}
