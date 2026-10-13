#include "kf_entity.h"
#include "kf_entity_world.h"

void func_001A32A8(Entity *);
char *func_0010CAB8(char *, u32);
int func_001A1C58(int, void *, void *, void *);

/* Respawns the entity, sets its flags for a fresh fight, and may start a bone effect. */
void func_0018DC40(Entity *entity) {
    char *bone;

    KF_Entity_ResetToSpawn(entity);
    entity->flags = ((entity->flags | 2) & ~0xC) | 0x100;
    func_001A32A8(entity);
    if (entity->params->unkC6 != 0) {
        if (entity->params->unkC6 != 0xFFFF) {
            bone = func_0010CAB8((char *)entity->model, entity->params->unkC5);
            func_001A1C58(entity->params->unkC6, bone, (char *)entity->model + 0xD0, entity);
        }
    }
}
