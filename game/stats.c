/* stats.c: PC-only. The game's side of the extended stats mod (port/include/port/stats.h,
   docs/port/EXTENDED_STATS.md): reads what the game keeps about the player into a snapshot for the
   overlay's panel, and watches the hits. It only reads the game's state, never writes it.

   Every number is the game's own, read where the game keeps it, and every rule below is the game
   code's (the function named beside it); the names of items and spells are read from the game's own
   tables (param/item_nam.prm, param/magic.prm in game RAM), so none is written here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "core/game.h"
#include "map/map.h"
#include "item/item.h"
#include "player/player.h"
#include "entity/entity.h"
#include "effect/effect.h"
#include "port/platform.h"
#include "port/stats.h"

#define GAUGE_FULL 5000 /* Player_UpdateStatus */

extern GameStatus game_status;
extern PlayerState player_state;
extern MapSlot map_slots[2];
extern int current_map_slot;
extern const PlayerLevelStep player_level_steps[];
extern u8 spell_levels[6][5];
extern u16 spell_experience[6][5];
extern LevelRecord weapon_level_records[70];
extern LevelRecord worn_item_level_records[160];
extern GameDefinition item_name_defs[];
extern PlayerItemDef weapon_defs[];
extern LevelRecordDef worn_defs[];
extern FxTableEntry fx_table[];
extern Entity *active_entities_head;
extern int recent_damage_frames;
extern void *menu_screen_handler;
extern s8 main_menu_line;
extern s8 menu_mode;
extern s32 menu_step;

/* ---- Names, from the game's own tables ---------------------------------------------------------- */

/* The game's text code (docs GAME_DATA.md, "The text code"): 0x08xx a Latin character, 0x8000 a line
   break, 0xFFFF the end; anything else (a Japanese glyph) shows as '?' */
static void decode_text(const u16 *text, int most_codes, char *out, size_t size) {
    size_t at = 0;
    int i;

    for (i = 0; i < most_codes && at + 1 < size; i++) {
        u16 code = text[i];

        if (code == 0xFFFF) {
            break;
        }
        if (code == 0) {
            continue;
        }
        if ((code & 0xFF00) == 0x0800) {
            out[at++] = (char)((code & 0xFF) + 0x20);
        } else if (code == 0x8000) {
            out[at++] = ' ';
        } else {
            out[at++] = '?';
        }
    }
    out[at] = 0;
}

static void item_name(int item, char *out, size_t size) {
    out[0] = 0;
    if (item >= 0 && item < ITEM_COUNT) {
        decode_text((const u16 *)&item_name_defs[item], 0x30 / 2, out, size);
    }
}

static void spell_name(int id, char *out, size_t size) {
    out[0] = 0;
    if (id > 0 && id < 0x100) {
        decode_text((const u16 *)&fx_table[id], 0x34 / 2, out, size);
    }
}

/* ---- The snapshot ---------------------------------------------------------------------------- */

static int in_game(void) {
    return map_slots[current_map_slot & 1].loaded && player_state.level >= 1 && player_state.hp_max > 0 &&
           !game_status.bit9;
}

static int ceil_div(int value, int by) {
    return by > 0 ? (value + by - 1) / by : 0;
}

static void add_effect(PortStatsSnapshot *s, int item, const char *text) {
    PortStatsEffect *e;

    if (s->effect_count >= PORT_STATS_EFFECTS) {
        return;
    }
    e = &s->effects[s->effect_count++];
    snprintf(e->text, sizeof(e->text), "%s", text);
    item_name(item, e->item, sizeof(e->item));
}

static void add_timer(PortStatsSnapshot *s, int frames, const char *text) {
    PortStatsTimer *t;

    if (frames <= 0 || s->timer_count >= PORT_STATS_TIMERS) {
        return;
    }
    t = &s->timers[s->timer_count++];
    snprintf(t->text, sizeof(t->text), "%s", text);
    t->frames = frames;
}

/* An item's two data bonuses (Player_AddItemBonuses' kinds) as effects */
static void bonus_effects(PortStatsSnapshot *s, int item, const u8 *kinds, const u8 *amounts) {
    char text[96];
    int i;

    for (i = 0; i < 2; i++) {
        int amount = amounts[i];

        text[0] = 0;
        switch (kinds[i]) {
        case 2:
            snprintf(text, sizeof(text), "Drains: Physical and Magic Power halved, -1 HP and -1 MP a second");
            break;
        case 3:
            snprintf(text, sizeof(text), "HP +%d every 5 seconds", amount);
            break;
        case 4:
            snprintf(text, sizeof(text), "HP -%d every 5 seconds", amount);
            break;
        case 5:
            snprintf(text, sizeof(text), "MP +%d every 5 seconds", amount);
            break;
        case 6:
            snprintf(text, sizeof(text), "MP -%d every 5 seconds", amount);
            break;
        case 7:
            snprintf(text, sizeof(text), "%d%% of the damage you deal comes back as HP", amount);
            break;
        case 8:
            snprintf(text, sizeof(text), "%d%% of the damage you deal comes back as MP", amount);
            break;
        case 9:
            snprintf(text, sizeof(text), "Physical Power +%d while worn", amount);
            break;
        case 10:
            snprintf(text, sizeof(text), "Magic Power +%d while worn", amount);
            break;
        }
        if (text[0]) {
            add_effect(s, item, text);
        }
    }
}

/* The effects the game code ties to particular items (docs/port/EXTENDED_STATS.md lists each site) */
static void fixed_effects(PortStatsSnapshot *s) {
    const PlayerState *p = &player_state;
    const u8 *eq = p->equipment;
    char text[96];
    int ancient_set = PLAYER_WEARS_ANCIENT_SET(p);

    /* Player_AddExperience */
    if (eq[EQUIP_BRACELET] == ITEM_WORN_INDEX(ITEM_FORTUNE)) {
        add_effect(s, ITEM_FORTUNE, "Experience gained x2");
    }
    /* Attack_HitEntity */
    if (p->weapon == 0x11) {
        add_effect(s, ITEM_WEAPON_FIRST + 0x11, "Double damage against some creatures (most of them undead)");
    }
    /* Player_UpdateStatus */
    if (p->weapon == 0x24) {
        add_effect(s, ITEM_WEAPON_FIRST + 0x24, "Every 20 s: 70% chance to poison you (no defence)");
    }
    /* Attack_HitPlayer */
    if ((u8)(eq[EQUIP_SHIELD] - 1) <= 1) {
        add_effect(s, ITEM_WORN_FIRST + eq[EQUIP_SHIELD], "Fire damage taken +20%");
    }
    if ((u8)(eq[EQUIP_BOOT] - 0x6A) <= 1) {
        add_effect(s, ITEM_WORN_FIRST + eq[EQUIP_BOOT], "Fire damage taken +20%");
    }
    /* Player_UpdateStatus: the ailments' wear */
    if (eq[EQUIP_ARMOR] == ITEM_WORN_INDEX(ITEM_DEMON_ARMOR)) {
        add_effect(s, ITEM_DEMON_ARMOR, "Ailments last 1.5x as long");
    }
    if (eq[EQUIP_AMULET] == ITEM_WORN_INDEX(ITEM_PURITY_AMULET)) {
        add_effect(s, ITEM_PURITY_AMULET, "Ailments last 0.75x as long");
    }
    /* Player_RecomputeStats */
    if (ancient_set) {
        add_effect(s, ITEM_ANCIENT_SWORD, "Whole Ancient set: its weight counts a third");
    } else if (eq[EQUIP_SHIELD] == ITEM_WORN_INDEX(ITEM_ANCIENT_SHIELD)) {
        add_effect(s, ITEM_ANCIENT_SHIELD, "Cannot run without the whole set"); /* Player_Update */
    }
    if (eq[EQUIP_GAUNTLET] == ITEM_WORN_INDEX(ITEM_SACRED_GLOVES)) {
        add_effect(s, ITEM_SACRED_GLOVES, "The weapon's weight counts 3/4");
    }
    if (eq[EQUIP_GAUNTLET] == ITEM_WORN_INDEX(ITEM_LORD_MYUS_GLOVES)) {
        add_effect(s, ITEM_LORD_MYUS_GLOVES, "The weapon's weight counts 3/4");
    }
    if (eq[EQUIP_GAUNTLET] == ITEM_WORN_INDEX(ITEM_BONE_HANDS)) {
        add_effect(s, ITEM_BONE_HANDS, "The weapon's weight counts twice");
    }
    if (eq[EQUIP_GAUNTLET] == ITEM_WORN_INDEX(ITEM_SILVERY_GLOVES)) {
        add_effect(s, ITEM_SILVERY_GLOVES, "Slash, strike and stab attack +10%");
    }
    if (eq[EQUIP_GAUNTLET] == ITEM_WORN_INDEX(ITEM_VENOM_GLOVES)) {
        add_effect(s, ITEM_VENOM_GLOVES, "Weapon's poison chance +70%");
    }
    if (eq[EQUIP_RING] == ITEM_WORN_INDEX(ITEM_WIDDAS_RING)) {
        add_effect(s, ITEM_WIDDAS_RING, "Most HP +20%");
    }
    if (eq[EQUIP_RING] >= ITEM_WORN_INDEX(ITEM_RING_OF_FIRE) && eq[EQUIP_RING] <= ITEM_WORN_INDEX(ITEM_RING_OF_WATER)) {
        static const char *const element[4] = {"Fire", "Earth", "Wind", "Water"};

        snprintf(text, sizeof(text), "%s attack +20", element[eq[EQUIP_RING] - ITEM_WORN_INDEX(ITEM_RING_OF_FIRE)]);
        add_effect(s, ITEM_WORN_FIRST + eq[EQUIP_RING], text);
    }
    if (eq[EQUIP_BRACELET] == ITEM_WORN_INDEX(ITEM_POWER_BRACELET)) {
        add_effect(s, ITEM_POWER_BRACELET, "Slash, strike and stab attack +10%");
    }
    if (eq[EQUIP_BRACELET] == ITEM_WORN_INDEX(ITEM_FEATHER_BRACELET)) {
        add_effect(s, ITEM_FEATHER_BRACELET, "Weight carried x3/4");
    }
    if (eq[EQUIP_HELM] == ITEM_WORN_INDEX(ITEM_ANGELIC_CROWN)) {
        add_effect(s, ITEM_ANGELIC_CROWN, "Most MP +20%");
    }
    if (eq[EQUIP_BRACELET] == ITEM_WORN_INDEX(ITEM_CLARITY_BRACELET)) {
        add_effect(s, ITEM_CLARITY_BRACELET, "Most MP x5 (up to 999)");
    }
    /* Player_CastMagic, Player_HoldMagic: the first school of spells (ids 1 to 15) */
    if (eq[EQUIP_ARMOR] == ITEM_WORN_INDEX(ITEM_ICE_ARMOR)) {
        char school[PORT_STATS_NAME];

        spell_name(1, school, sizeof(school));
        snprintf(text, sizeof(text), "Cannot cast the school of %s", school);
        add_effect(s, ITEM_ICE_ARMOR, text);
    }
    /* the enemies' senses and reach (entity/entity_ai.c, entity_ai_choice.c) */
    if (eq[EQUIP_AMULET] == ITEM_WORN_INDEX(ITEM_THIEVES_NECKLACE)) {
        add_effect(s, ITEM_THIEVES_NECKLACE, "Enemies notice and reach you from half as far");
    }
    /* Player_TakeFallDamage: the landing's slowdown (landing_slow_timer) */
    if (eq[EQUIP_BOOT] == ITEM_WORN_INDEX(ITEM_ABSORPTION_BOOTS)) {
        add_effect(s, ITEM_ABSORPTION_BOOTS, "No slowdown after a hard landing");
    }
    /* Player_AddItemBonuses' own items */
    if (p->movement_halved) {
        add_effect(s, ITEM_DEMON_LEGS, "Half speed, cannot run");
    }
    if (p->ring_of_sight_active) {
        add_effect(s, ITEM_RING_OF_SIGHT, "Lights the dark as the Light spell does");
    }
    /* Player_UpdateBreath: the amulet 374 */
    if (eq[EQUIP_AMULET] == 0x9A) {
        add_effect(s, ITEM_WORN_FIRST + 0x9A, "Breath under water lasts twice as long");
    }
    /* Player_Update */
    if (eq[EQUIP_HELM] == ITEM_WORN_INDEX(ITEM_SACRIFICIAL_MASK)) {
        add_effect(s, ITEM_SACRIFICIAL_MASK, "Now and then casts a magic of its own (1 in 451 a frame)");
    }
    /* Attack_HitMapObject */
    if (p->weapon == 0x2E) {
        add_effect(s, ITEM_WEAPON_FIRST + 0x2E, "Breaks what other weapons bounce off");
    }
    /* MapObject_DrawAll */
    if (eq[EQUIP_AMULET] == ITEM_WORN_INDEX(ITEM_MYUS_NECKLACE)) {
        add_effect(s, ITEM_MYUS_NECKLACE, "Some objects glow");
    }
}

static void timers(PortStatsSnapshot *s) {
    static const char *const kinds[9] = {"Slash", "Strike", "Stab", "Fire", "Earth", "Wind", "Water", "Light", "Dark"};
    static const char *const ailments[6] = {"Poison", "Paralysis", "Darkness", "Curse", "Slow", "Magic seal"};
    const PlayerState *p = &player_state;
    char text[64];
    int i;

    for (i = 0; i < 3; i++) {
        if (p->physical_strength_bonus[i] != 0) {
            snprintf(text, sizeof(text), "%s attack %+d", kinds[i], p->physical_strength_bonus[i]);
            add_timer(s, p->physical_strength_bonus_timer[i], text);
        }
        if (p->physical_defence_bonus[i] != 0) {
            snprintf(text, sizeof(text), "%s defence %+d", kinds[i], p->physical_defence_bonus[i]);
            add_timer(s, p->physical_defence_bonus_timer[i], text);
        }
    }
    for (i = 0; i < 6; i++) {
        if (p->magic_strength_bonus[i] != 0) {
            snprintf(text, sizeof(text), "%s attack %+d", kinds[3 + i], p->magic_strength_bonus[i]);
            add_timer(s, p->magic_strength_bonus_timer[i], text);
        }
        if (p->magic_defence_bonus[i] != 0) {
            snprintf(text, sizeof(text), "%s defence %+d", kinds[3 + i], p->magic_defence_bonus[i]);
            add_timer(s, p->magic_defence_bonus_timer[i], text);
        }
        if (p->ailment_attack_bonus[i] != 0) {
            snprintf(text, sizeof(text), "%s chance of the weapon +%d%%", ailments[i], p->ailment_attack_bonus[i]);
            add_timer(s, p->ailment_attack_bonus_timer[i], text);
        }
        if (p->ailment_defence_bonus[i] != 0) {
            snprintf(text, sizeof(text), "%s defence +%d%%", ailments[i], p->ailment_defence_bonus[i]);
            add_timer(s, p->ailment_defence_bonus_timer[i], text);
        }
    }
    add_timer(s, p->power_seed_timer, "Physical Power +10%");
    add_timer(s, p->widda_needle_timer, "Magic Power +10%");
    if (p->rage_timer > 0) {
        snprintf(text, sizeof(text), "Physical Power +%d, Magic Power 1", p->rage_power_bonus);
        add_timer(s, p->rage_timer, text);
    }
    add_timer(s, p->light_spell_timer, "Light");
    add_timer(s, p->intimidate_timer, "Enemies attack from half as far");
}

int port_stats_sample(PortStatsSnapshot *out) {
    const PlayerState *p = &player_state;
    int i, slot, rate, step, wear_frames;

    memset(out, 0, sizeof(*out));
    if (!in_game()) {
        return 0;
    }
    out->valid = 1;
    out->in_menu = menu_mode == 0 && menu_step != 0;

    /* Player_AddExperience: level L is reached at player_level_steps[L].experience */
    out->level = p->level;
    out->experience = p->experience;
    out->level_from = p->level < 100 ? player_level_steps[p->level].experience : 0;
    if (p->level + 1 < 100) {
        const PlayerLevelStep *next = &player_level_steps[p->level + 1];

        out->level_to = next->experience;
        out->next_hp = next->hp_gain;
        out->next_mp = next->mp_gain;
        out->next_power = next->power_gain;
        out->next_magic = next->magic_gain;
    }
    out->fortune = p->equipment[EQUIP_BRACELET] == ITEM_WORN_INDEX(ITEM_FORTUNE);

    /* Player_CountWeaponUse, Player_CountMagicUse: every 100th use raises the base by one */
    out->power_base = p->power_base;
    out->power = p->power;
    out->power_uses = p->weapon_use_count;
    out->magic_base = p->magic_base;
    out->magic = p->magic;
    out->magic_uses = p->magic_use_count;

    /* Player_UpdateStatus: each frame the gauge gains max(power, 20) * 42 / (weapon weight + 1) */
    out->power_gauge = p->power_gauge;
    out->magic_gauge = p->magic_gauge;
    if (weapon_defs[p->weapon].action_handler_index < 2) {
        rate = p->power < 20 ? 20 : p->power;
        step = rate * 42 / (item_name_defs[p->weapon + ITEM_WEAPON_FIRST].weight_tenths_of_kilogram + 1);
        out->power_fill_frames = ceil_div(GAUGE_FULL, step);
    }
    if (p->charging_magic != 0) {
        rate = p->magic < 20 ? 20 : p->magic;
        step = rate * 42 / (fx_table[p->charging_magic].gauge_charge_divisor + 1);
        out->magic_fill_frames = ceil_div(GAUGE_FULL, step);
        spell_name(p->charging_magic, out->charging_magic, sizeof(out->charging_magic));
    }

    out->weight = p->weight;
    out->max_weight = p->max_weight;
    for (i = 0; i < 9; i++) {
        out->attack[i] = p->attack_stats.values[i];
        out->defence[i] = p->defence_stats.values[i];
    }
    /* Attack_RollPlayerAilments, Player_UpdateStatus (WEAR_OFF) */
    wear_frames = 3;
    if (p->equipment[EQUIP_ARMOR] == ITEM_WORN_INDEX(ITEM_DEMON_ARMOR)) {
        wear_frames--;
    }
    if (p->equipment[EQUIP_AMULET] == ITEM_WORN_INDEX(ITEM_PURITY_AMULET)) {
        wear_frames++;
    }
    out->ailment_wear_thirds = wear_frames;
    for (i = 0; i < 6; i++) {
        out->ailment_defence[i] = p->ailment_defence[i];
        out->ailment_frames[i] = (u16)p->timed_flags[i];
    }
    /* Player_SumWornBonuses: a period of 150 / the sum of the amounts */
    out->hp_regen_period = (int)p->hp_regen_period;
    out->hp_drain_period = (int)p->hp_drain_period;
    out->mp_regen_period = (int)p->mp_regen_period;
    out->mp_drain_period = (int)p->mp_drain_period;
    out->damage_to_hp = p->damage_to_hp_percent;
    out->damage_to_mp = p->damage_to_mp_percent;
    out->drain_curse = p->worn_drain_active != 0;
    /* Attack_HitPlayer */
    out->fire_taken_percent = (u8)(p->equipment[EQUIP_SHIELD] - 1) <= 1 ? 120 : 100;
    if ((u8)(p->equipment[EQUIP_BOOT] - 0x6A) <= 1) {
        out->fire_taken_percent += 20;
    }

    /* the weapon and the worn items: LevelRecord (Attack_WearWeapon, Attack_WearWornItem) */
    for (slot = 0; slot < PORT_STATS_SLOTS; slot++) {
        PortStatsItem *it = &out->items[slot];
        const LevelRecord *record;
        int index;

        if (slot == 0) {
            index = p->weapon;
            it->item = ITEM_WEAPON_FIRST + index;
            record = &weapon_level_records[index];
            it->none = index == 0;
            it->wears = index != 0 && weapon_defs[index].action_handler_index < 2;
            it->weapon_level = record->level_index + 1;
            it->weapon_uses = (u16)record->use_count;
            bonus_effects(out, it->item, weapon_defs[index].bonus_kinds, weapon_defs[index].bonus_amounts);
        } else {
            index = p->equipment[slot - 1];
            it->item = ITEM_WORN_FIRST + index;
            record = &worn_item_level_records[index];
            it->none = it->item == ITEM_SHIELD_NONE || it->item == ITEM_HELM_NONE || it->item == ITEM_ARMOR_NONE ||
                       it->item == ITEM_GAUNTLET_NONE || it->item == ITEM_BOOT_NONE || it->item == ITEM_RING_NONE ||
                       it->item == ITEM_BRACELET_NONE || it->item == ITEM_AMULET_NONE;
            /* Attack_WearArmorFromHit wears the shield, helm, armour, gloves and boots */
            it->wears = !it->none && slot - 1 <= EQUIP_BOOT;
            bonus_effects(out, it->item, worn_defs[index].bonus_kinds, worn_defs[index].bonus_amounts);
        }
        item_name(it->item, it->name, sizeof(it->name));
        it->durability = record->level;
        it->wear = record->exp;
        it->wear_per_point = ((const LevelDef *)item_name_defs)[it->item].exp_per_level;
    }

    /* spells: the effect of school r, slot c at level L is (r * 5 + c) * 3 + L (Player_CountMagicUse) */
    for (i = 0; i < PORT_STATS_SPELLS; i++) {
        int level = spell_levels[i / 5][i % 5];

        if (level == 0) {
            continue;
        }
        PortStatsSpell *sp = &out->spells[out->spell_count++];
        sp->level = level;
        sp->experience = spell_experience[i / 5][i % 5];
        spell_name(i * 3 + level, sp->name, sizeof(sp->name));
    }

    fixed_effects(out);
    timers(out);
    return 1;
}

/* ---- The hits -------------------------------------------------------------------------------- */
#define TRACKED 96
#define LOG_SIZE 64

typedef struct {
    const Entity *entity;
    const void *spawn;
    int cooldown, hp, id;
    int seen;
} Tracked;

static Tracked tracked[TRACKED];
static int next_id = 1;
static PortStatsHit hit_log[LOG_SIZE];
static int hit_count; /* how many were ever logged; the last LOG_SIZE are kept */
static PortStatsHit target;
static int player_hp = -1, player_recent;
static int was_in_game;
/* the Status screen: the handler of the main menu, learned when its cursor moves */
static void *main_handler, *last_handler;
static int last_line = -1;
static int status_screen;
static int log_on = -1;

static void log_hit(const PortStatsHit *hit) {
    hit_log[hit_count % LOG_SIZE] = *hit;
    hit_count++;
    if (log_on) {
        fprintf(stderr, "[stats] vblank %llu: %s %d (HP %d/%d)%s\n", hit->vblank,
                hit->kind == PORT_STATS_HIT_DEALT   ? "dealt"
                : hit->kind == PORT_STATS_HIT_TAKEN ? "taken"
                                                    : "lost",
                hit->amount, hit->left, hit->max, hit->killed ? " killed" : "");
    }
}

static void track_entities(unsigned long long vblank) {
    const Entity *e;
    int i;

    for (i = 0; i < TRACKED; i++) {
        tracked[i].seen = 0;
    }
    for (e = active_entities_head; e != NULL; e = e->next) {
        Tracked *t = NULL;
        int cooldown = e->hit_cooldown_frames, hp = e->hit_points;

        for (i = 0; i < TRACKED; i++) {
            if (tracked[i].entity == e && tracked[i].spawn == e->spawn_info) {
                t = &tracked[i];
                break;
            }
        }
        if (t == NULL) {
            for (i = 0; i < TRACKED; i++) {
                if (tracked[i].entity == NULL) {
                    t = &tracked[i];
                    t->entity = e;
                    t->spawn = e->spawn_info;
                    t->cooldown = cooldown;
                    t->hp = hp;
                    t->id = next_id++;
                    if (log_on && e->params != NULL && e->model != NULL) {
                        uint32_t mxcsr = port_host_float_begin();

                        fprintf(stderr, "[stats] creature %d: type %d, HP %d/%d, at %.0f,%.0f,%.0f\n", t->id,
                                e->spawn_info != NULL ? e->spawn_info->type_id : -1, hp, e->params->hit_points,
                                e->model->position[0], e->model->position[1], e->model->position[2]);
                        port_host_float_end(mxcsr);
                    }
                    break;
                }
            }
            if (t == NULL) {
                continue;
            }
        }
        t->seen = 1;
        /* Attack_HitEntity: a hit that takes HP sets hit_cooldown_frames to 15 and last_hit_damage; no hit
           lands while the cooldown runs, so a cooldown that rose is a new hit */
        if (cooldown > t->cooldown && hp < t->hp && e->params != NULL) {
            PortStatsHit hit;

            hit.kind = PORT_STATS_HIT_DEALT;
            hit.amount = e->last_hit_damage;
            hit.left = hp;
            hit.max = e->params->hit_points;
            hit.killed = hp == 0;
            hit.vblank = vblank;
            hit.target = t->id;
            log_hit(&hit);
            target = hit;
        }
        t->cooldown = cooldown;
        t->hp = hp;
    }
    for (i = 0; i < TRACKED; i++) {
        if (!tracked[i].seen) {
            tracked[i].entity = NULL;
        }
    }
}

static void track_player(unsigned long long vblank) {
    int hp = player_state.hp_now, recent = recent_damage_frames;

    if (player_hp >= 0 && hp < player_hp) {
        PortStatsHit hit;

        /* Player_TakeDamage sets recent_damage_frames to 6; poison, a curse and the ground do not */
        hit.kind = recent > player_recent ? PORT_STATS_HIT_TAKEN : PORT_STATS_HIT_OTHER;
        hit.amount = player_hp - hp;
        hit.left = hp;
        hit.max = player_state.hp_max;
        hit.killed = hp == 0;
        hit.vblank = vblank;
        hit.target = 0;
        log_hit(&hit);
    }
    player_hp = hp;
    player_recent = recent;
}

/* The game's Status screen (main menu line 3, Menu_UpdateStatus): the main menu's handler is learned
   as its cursor moves (main_menu_line changes only there); a different handler with line 3 chosen is
   the screen line 3 opened */
static void track_menu(void) {
    void *handler = menu_screen_handler;
    int line = main_menu_line;
    int in_menu = menu_mode == 0 && menu_step != 0;

    if (!in_menu) {
        status_screen = 0;
        last_line = line;
        last_handler = handler;
        return;
    }
    if (line != last_line && handler == last_handler && handler != NULL) {
        main_handler = handler;
    }
    status_screen = line == 3 && main_handler != NULL && handler != main_handler && handler != NULL;
    last_line = line;
    last_handler = handler;
}

static void log_progress(void) {
    static int last[11] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    const LevelRecord *shield = &worn_item_level_records[player_state.equipment[EQUIP_SHIELD]];
    int now[11];
    int i, changed = 0;

    now[0] = player_state.weapon_use_count;
    now[1] = player_state.power_base;
    now[2] = player_state.magic_use_count;
    now[3] = player_state.magic_base;
    now[4] = (u16)weapon_level_records[player_state.weapon].use_count;
    now[5] = weapon_level_records[player_state.weapon].level_index;
    now[6] = weapon_level_records[player_state.weapon].level * 1000 + weapon_level_records[player_state.weapon].exp;
    now[7] = player_state.experience;
    now[8] = player_state.mp_now;
    now[9] = spell_experience[0][0] * 10 + spell_levels[0][0];
    now[10] = shield->level * 1000 + shield->exp;
    for (i = 0; i < 11; i++) {
        changed |= now[i] != last[i];
        last[i] = now[i];
    }
    if (changed) {
        fprintf(stderr,
                "[stats] weapon_use_count=%d power_base=%d magic_use_count=%d magic_base=%d weapon_uses=%d "
                "weapon_level=%d durability=%d wear=%d exp=%d mp=%d spell0=%d/%d shield=%d wear=%d\n",
                now[0], now[1], now[2], now[3], now[4], now[5] + 1, now[6] / 1000, now[6] % 1000, now[7], now[8],
                now[9] / 10, now[9] % 10, now[10] / 1000, now[10] % 1000);
    }
}

void port_stats_track(void) {
    unsigned long long vblank = port_vblank_count();
    int playing;

    if (log_on < 0) {
        log_on = getenv("KF4_STATS_LOG") != NULL;
    }
    if (!port_stats_options.on) {
        was_in_game = 0;
        return;
    }
    playing = in_game();
    if (!playing || !was_in_game) {
        /* a new game, a load or the title: nothing carries over */
        memset(tracked, 0, sizeof(tracked));
        player_hp = -1;
    }
    was_in_game = playing;
    track_menu();
    if (!playing) {
        return;
    }
    if (log_on) {
        log_progress();
    }
    track_entities(vblank);
    track_player(vblank);
}

int port_stats_status_screen(void) {
    return status_screen;
}

int port_stats_hits(PortStatsHit *out, int most, unsigned long long since) {
    int first = hit_count > LOG_SIZE ? hit_count - LOG_SIZE : 0;
    int i, n = 0;

    for (i = first; i < hit_count; i++) {
        if (hit_log[i % LOG_SIZE].vblank >= since) {
            break;
        }
    }
    if (hit_count - i > most) {
        i = hit_count - most;
    }
    for (; i < hit_count; i++) {
        out[n++] = hit_log[i % LOG_SIZE];
    }
    return n;
}

int port_stats_target(PortStatsHit *out, unsigned long long vblanks) {
    if (target.target == 0 || port_vblank_count() - target.vblank > vblanks) {
        return 0;
    }
    *out = target;
    return 1;
}
