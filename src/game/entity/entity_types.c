/* Per-entity-type handlers and the Fire Lord's model templates. */
#include "entity/entity_ai_handlers.h"
#include "model/model.h"

/* the set-up function of every entity type that has one of its own (enemy/*.c, entity/npc.c) */
void AcidCrawler_Init(Entity *entity);
void AncientKing_Init(Entity *entity);
void AncientMummy_Init(Entity *entity);
void Aqualizard_Init(Entity *entity);
void Beetle_Init(Entity *entity);
void Bell_Init(Entity *entity);
void BloodBat_Init(Entity *entity);
void Critter_Init(Entity *entity);
void Crow_Init(Entity *entity);
void DarkArcher_Init(Entity *entity);
void DarkBird_Init(Entity *entity);
void DarkFlower_Init(Entity *entity);
void DarkKnight_Init(Entity *entity);
void DarkOne_Init(Entity *entity);
void DarkPuppet_Init(Entity *entity);
void DarkSoldier_Init(Entity *entity);
void DarkSorcerer_Init(Entity *entity);
void DarkSuccubus_Init(Entity *entity);
void DragonFly_Init(Entity *entity);
void EarthElemental_Init(Entity *entity);
void EarthKing_Init(Entity *entity);
void EntityType72_Init(Entity *entity);
void ExpeditionArcher_Init(Entity *entity);
void ExpeditionSoldier_Init(Entity *entity);
void EyePedestal_Init(Entity *entity);
void Fadin_Init(Entity *entity);
void FireElemental_Init(Entity *entity);
void FireGenie_Init(Entity *entity);
void FireLord_Init(Entity *entity);
void ForestGolem_Init(Entity *entity);
void GiantBeetle_Init(Entity *entity);
void GiantLeader_Init(Entity *entity);
void GiantSpider_Init(Entity *entity);
void Giant_Init(Entity *entity);
void GoldenStatue_Init(Entity *entity);
void GreaterDemon_Init(Entity *entity);
void HammerSkeleton_Init(Entity *entity);
void HighSkeleton_Init(Entity *entity);
void IceGolem_Init(Entity *entity);
void Larva_Init(Entity *entity);
void LavaBird_Init(Entity *entity);
void LavaFace_Init(Entity *entity);
void LavaLizard_Init(Entity *entity);
void LivingArmor_Init(Entity *entity);
void LivingDead_Init(Entity *entity);
void LivingStatue_Init(Entity *entity);
void Makara_Init(Entity *entity);
void MalformedGiant_Init(Entity *entity);
void Malice_Init(Entity *entity);
void MasterFadin_Init(Entity *entity);
void Minion_Init(Entity *entity);
void Mummy_Init(Entity *entity);
void MutantCeiling_Init(Entity *entity);
void MutantWall_Init(Entity *entity);
void Npc_Init(Entity *entity);
void Npc_InitType81(Entity *entity);
void Onyx_Init(Entity *entity);
void ParaStool_Init(Entity *entity);
void Phagos_Init(Entity *entity);
void Reaper_Init(Entity *entity);
void Salamander_Init(Entity *entity);
void Scorpion_Init(Entity *entity);
void Seafang_Init(Entity *entity);
void Sentinel_Init(Entity *entity);
void SkeletonArcher_Init(Entity *entity);
void SkeletonSoldier_Init(Entity *entity);
void Skull_Init(Entity *entity);
void Slasher_Init(Entity *entity);
void Slime_Init(Entity *entity);
void Snake_Init(Entity *entity);
void Spider_Init(Entity *entity);
void StoneGuardian_Init(Entity *entity);
void Stool_Init(Entity *entity);
void Watcher_Init(Entity *entity);
void WaterElemental_Init(Entity *entity);
void WiddaArcher_Init(Entity *entity);
void WiddaQueen_Init(Entity *entity);
void Widda_Init(Entity *entity);
void Widdacona_Init(Entity *entity);
void WindElemental_Init(Entity *entity);
void Zombie_Init(Entity *entity);
void Entity_InitInert(Entity *entity);
void Entity_UpdateInert();

/* 0x3E6E40 [type][0] set-up, [type][1] update; types without a row act as type 0 */
void (*entity_type_handlers[0x60][2])(Entity *) = {
    EntityAi_InitDefaults, EntityAi_Update, Slime_Init, EntityAi_Update,
    Reaper_Init, EntityAi_Update, Spider_Init, EntityAi_Update,
    GiantSpider_Init, EntityAi_Update, Beetle_Init, EntityAi_Update,
    SkeletonSoldier_Init, EntityAi_Update, ExpeditionSoldier_Init, EntityAi_Update,
    Stool_Init, EntityAi_Update, SkeletonArcher_Init, EntityAi_Update,
    BloodBat_Init, EntityAi_Update, LivingDead_Init, EntityAi_Update,
    Crow_Init, EntityAi_Update, ExpeditionArcher_Init, EntityAi_Update,
    Phagos_Init, EntityAi_Update, Npc_Init, EntityAi_Update,
    Scorpion_Init, EntityAi_Update, DragonFly_Init, EntityAi_Update,
    AcidCrawler_Init, EntityAi_Update, Widda_Init, EntityAi_Update,
    WiddaArcher_Init, EntityAi_Update, Snake_Init, EntityAi_Update,
    Salamander_Init, EntityAi_Update, StoneGuardian_Init, EntityAi_Update,
    FireLord_Init, EntityAi_Update, Critter_Init, EntityAi_Update,
    Slasher_Init, EntityAi_Update, Entity_InitInert, Entity_UpdateInert,
    Seafang_Init, EntityAi_Update, GoldenStatue_Init, EntityAi_Update,
    Widdacona_Init, EntityAi_Update, EyePedestal_Init, EntityAi_Update,
    LavaFace_Init, EntityAi_Update, Sentinel_Init, EntityAi_Update,
    LavaLizard_Init, EntityAi_Update, Giant_Init, EntityAi_Update,
    LivingArmor_Init, EntityAi_Update, HammerSkeleton_Init, EntityAi_Update,
    Bell_Init, EntityAi_Update, Mummy_Init, EntityAi_Update,
    Zombie_Init, EntityAi_Update, MalformedGiant_Init, EntityAi_Update,
    AncientMummy_Init, EntityAi_Update, ParaStool_Init, EntityAi_Update,
    DarkSoldier_Init, EntityAi_Update, EarthKing_Init, EntityAi_Update,
    WiddaQueen_Init, EntityAi_Update, IceGolem_Init, EntityAi_Update,
    FireElemental_Init, EntityAi_Update, EarthElemental_Init, EntityAi_Update,
    WindElemental_Init, EntityAi_Update, WaterElemental_Init, EntityAi_Update,
    DarkPuppet_Init, EntityAi_Update, DarkSorcerer_Init, EntityAi_Update,
    DarkSuccubus_Init, EntityAi_Update, DarkArcher_Init, EntityAi_Update,
    GreaterDemon_Init, EntityAi_Update, Aqualizard_Init, EntityAi_Update,
    Malice_Init, EntityAi_Update, DarkBird_Init, EntityAi_Update,
    Minion_Init, EntityAi_Update, AncientKing_Init, EntityAi_Update,
    HighSkeleton_Init, EntityAi_Update, GiantLeader_Init, EntityAi_Update,
    DarkKnight_Init, EntityAi_Update, Skull_Init, EntityAi_Update,
    FireGenie_Init, EntityAi_Update, Larva_Init, EntityAi_Update,
    LavaBird_Init, EntityAi_Update, MutantCeiling_Init, EntityAi_Update,
    MutantWall_Init, EntityAi_Update, DarkFlower_Init, EntityAi_Update,
    EntityType72_Init, EntityAi_Update, Watcher_Init, EntityAi_Update,
    Onyx_Init, EntityAi_Update, DarkOne_Init, EntityAi_Update,
    GiantBeetle_Init, EntityAi_Update, ForestGolem_Init, EntityAi_Update,
    Makara_Init, EntityAi_Update, MasterFadin_Init, EntityAi_Update,
    Fadin_Init, EntityAi_Update, Npc_InitType81, EntityAi_Update,
    LivingStatue_Init, EntityAi_Update, Entity_InitInert, Entity_UpdateInert,
    Entity_InitInert, Entity_UpdateInert, Entity_InitInert, Entity_UpdateInert,
    Entity_InitInert, Entity_UpdateInert, Entity_InitInert, Entity_UpdateInert,
    Entity_InitInert, Entity_UpdateInert, Entity_InitInert, Entity_UpdateInert,
    Entity_InitInert, Entity_UpdateInert, Entity_InitInert, Entity_UpdateInert,
    Entity_InitInert, Entity_UpdateInert, Entity_InitInert, Entity_UpdateInert,
    Entity_InitInert, Entity_UpdateInert, Entity_InitInert, Entity_UpdateInert,
};

/* 0x3E7140 the Fire Lord's model, copied for its two draws; [1] (0x3E7300) is the older afterimage, the
   model two frames back (an asm symbol, fire_lord_older_afterimage, named it on its own on the PS2) */
ModelInstanceBlock fire_lord_model[2] = { 0 };

/* 0x3E74C0 the Fire Lord's model matrix (second row zero in retail) */
ModelMatrix fire_lord_model_matrix = {
    { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f },
      { 0.0f, -5.0f, 0.0f, 1.0f } },
};
