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
#include "gfx/packet.h"
#include "ui/menu.h"
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
extern ItemCommonDef item_common_defs[];
/* the game's menu (ui/menu_screens.c) */
extern void *menu_screen_handler;
extern s8 main_menu_line;
extern s8 main_menu_state;
extern s8 menu_mode;
extern s32 menu_step;
extern s32 use_item_frames;
extern DetailPanelFlags detail_panel_flags; /* detail_panel_open and equipped_list_open (ui/menu.h) */
extern f32 screen_part_alphas[0x100];
extern MenuItem *item_list_cursor[4];
extern MenuItem *owned_list_current[];
extern u32 main_part_count, status_part_count, equip_part_count, item_part_count, recol_part_count;
extern SpritePacket *main_parts, *status_parts, *equip_parts, *item_parts, *recol_parts;

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

/* What the game code does for a particular item while it is held or worn (docs/port/EXTENDED_STATS.md lists
   each site): its text, or 0 when nothing. The Ancient set's is in ancient_effect (it takes the whole set). */
static int item_code_effect(int item, char *text, size_t size) {
    char school[PORT_STATS_NAME];

    text[0] = 0;
    switch (item) {
    case ITEM_FORTUNE: /* Player_AddExperience */
        snprintf(text, size, "Experience gained x2");
        break;
    case ITEM_WEAPON_FIRST + 0x11: /* Attack_HitEntity */
        snprintf(text, size, "Double damage against some creatures (most of them undead)");
        break;
    case ITEM_WEAPON_FIRST + 0x24: /* Player_UpdateStatus */
        snprintf(text, size, "Every 20 s: 70%% chance to poison you (no defence)");
        break;
    case ITEM_WEAPON_FIRST + 0x2E: /* Attack_HitMapObject */
        snprintf(text, size, "Breaks what other weapons bounce off");
        break;
    case ITEM_WORN_FIRST + 1: /* Attack_HitPlayer: shields 1-2, boots 0x6A-0x6B */
    case ITEM_WORN_FIRST + 2:
    case ITEM_WORN_FIRST + 0x6A:
    case ITEM_WORN_FIRST + 0x6B:
        snprintf(text, size, "Fire damage taken +20%%");
        break;
    case ITEM_DEMON_ARMOR: /* Player_UpdateStatus: the ailments' wear */
        snprintf(text, size, "Ailments last 1.5x as long");
        break;
    case ITEM_PURITY_AMULET:
        snprintf(text, size, "Ailments last 0.75x as long");
        break;
    case ITEM_SACRED_GLOVES: /* Player_RecomputeStats */
    case ITEM_LORD_MYUS_GLOVES:
        snprintf(text, size, "The weapon's weight counts 3/4");
        break;
    case ITEM_BONE_HANDS:
        snprintf(text, size, "The weapon's weight counts twice");
        break;
    case ITEM_SILVERY_GLOVES:
    case ITEM_POWER_BRACELET:
        snprintf(text, size, "Slash, strike and stab attack +10%%");
        break;
    case ITEM_VENOM_GLOVES:
        snprintf(text, size, "Weapon's poison chance +70%%");
        break;
    case ITEM_WIDDAS_RING:
        snprintf(text, size, "Most HP +20%%");
        break;
    case ITEM_RING_OF_FIRE:
    case ITEM_RING_OF_EARTH:
    case ITEM_RING_OF_WIND:
    case ITEM_RING_OF_WATER: {
        static const char *const element[4] = {"Fire", "Earth", "Wind", "Water"};

        snprintf(text, size, "%s attack +20", element[item - ITEM_RING_OF_FIRE]);
        break;
    }
    case ITEM_FEATHER_BRACELET:
        snprintf(text, size, "Weight carried x3/4");
        break;
    case ITEM_ANGELIC_CROWN:
        snprintf(text, size, "Most MP +20%%");
        break;
    case ITEM_CLARITY_BRACELET:
        snprintf(text, size, "Most MP x5 (up to 999)");
        break;
    case ITEM_ICE_ARMOR: /* Player_CastMagic, Player_HoldMagic: the first school of spells (ids 1 to 15) */
        spell_name(1, school, sizeof(school));
        snprintf(text, size, "Cannot cast the school of %s", school);
        break;
    case ITEM_THIEVES_NECKLACE: /* the enemies' senses and reach (entity/entity_ai.c, entity_ai_choice.c) */
        snprintf(text, size, "Enemies notice and reach you from half as far");
        break;
    case ITEM_ABSORPTION_BOOTS: /* Player_TakeFallDamage: the landing's slowdown (landing_slow_timer) */
        snprintf(text, size, "No slowdown after a hard landing");
        break;
    case ITEM_DEMON_LEGS: /* Player_AddItemBonuses: movement_halved */
        snprintf(text, size, "Half speed, cannot run");
        break;
    case ITEM_RING_OF_SIGHT: /* Player_AddItemBonuses: ring_of_sight_active */
        snprintf(text, size, "Lights the dark as the Light spell does");
        break;
    case ITEM_WORN_FIRST + 0x9A: /* Player_UpdateBreath: the amulet 374 */
        snprintf(text, size, "Breath under water lasts twice as long");
        break;
    case ITEM_SACRIFICIAL_MASK: /* Player_Update */
        snprintf(text, size, "Now and then casts a magic of its own (1 in 451 a frame)");
        break;
    case ITEM_MYUS_NECKLACE: /* MapObject_DrawAll */
        snprintf(text, size, "Some objects glow");
        break;
    }
    return text[0] != 0;
}

/* The six pieces of the Ancient set (PLAYER_WEARS_ANCIENT_SET) */
static int ancient_piece(int item) {
    return item == ITEM_ANCIENT_SWORD || item == ITEM_ANCIENT_SHIELD || item == ITEM_ANCIENT_HELMET ||
           item == ITEM_ANCIENT_ARMOR || item == ITEM_ANCIENT_GLOVES || item == ITEM_ANCIENT_GREAVES;
}

/* The effects the game code ties to what is held and worn now */
static void fixed_effects(PortStatsSnapshot *s) {
    const PlayerState *p = &player_state;
    char text[96];
    int slot;

    if (item_code_effect(ITEM_WEAPON_FIRST + p->weapon, text, sizeof(text))) {
        add_effect(s, ITEM_WEAPON_FIRST + p->weapon, text);
    }
    for (slot = 0; slot < 8; slot++) {
        if (item_code_effect(ITEM_WORN_FIRST + p->equipment[slot], text, sizeof(text))) {
            add_effect(s, ITEM_WORN_FIRST + p->equipment[slot], text);
        }
    }
    /* Player_RecomputeStats; Player_Update */
    if (PLAYER_WEARS_ANCIENT_SET(p)) {
        add_effect(s, ITEM_ANCIENT_SWORD, "Whole Ancient set: its weight counts a third");
    } else if (p->equipment[EQUIP_SHIELD] == ITEM_WORN_INDEX(ITEM_ANCIENT_SHIELD)) {
        add_effect(s, ITEM_ANCIENT_SHIELD, "Cannot run without the whole set");
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

static void sample_menu(PortStatsMenu *m);

int port_stats_sample(PortStatsSnapshot *out) {
    const PlayerState *p = &player_state;
    int i, slot, rate, step, wear_frames;

    memset(out, 0, sizeof(*out));
    if (!in_game()) {
        return 0;
    }
    out->valid = 1;
    out->in_menu = menu_mode == 0 && menu_step != 0;
    sample_menu(&out->menu);

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
/* the game's menu: the main menu's handler, learned while it is up (track_menu) */
static void *main_handler;
static s32 last_use_frames = -1;
static unsigned long long use_item_at;
static int menu_screen;
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

/* Which screen of the game's menu is up (ui/menu_screens.c). The screens are handlers in
   menu_screen_handler, functions of the game's own that cannot be named from here, so they are told
   apart by what they do:
   - only Menu_UpdateMain moves main_menu_state off 0 (1 while it takes input, 2 and 3 while it fades out),
     and a screen it opens finds it back at 0: the handler while it is not 0 is the main menu's;
   - another handler is the screen of the main menu's line (main_menu_screens: 0 Item, 2 Equipment,
     3 Status, 5 Arrange), main_menu_line staying on the line chosen until the menu closes;
   - the Item screen's own sub-screen (the keys, Menu_UpdateKeyItems) is told from it by use_item_frames,
     which Menu_UpdateUseItem alone steps every time it runs.
   The one-frame handlers between (Menu_OpenMain, Menu_OpenChosenScreen) count as the screen of the line,
   whose parts are faded out then. */
static void track_menu(void) {
    void *handler = menu_screen_handler;
    unsigned long long now = port_vblank_count();

    if (use_item_frames != last_use_frames) {
        last_use_frames = use_item_frames;
        use_item_at = now;
    }
    menu_screen = PORT_STATS_MENU_NONE;
    if (menu_mode != 0 || menu_step == 0 || handler == NULL) {
        return;
    }
    if (main_menu_state != 0) {
        main_handler = handler;
    }
    if (handler == main_handler) {
        menu_screen = PORT_STATS_MENU_MAIN;
        return;
    }
    switch (main_menu_line) {
    case 0:
        menu_screen = now - use_item_at <= 4 ? PORT_STATS_MENU_ITEMS : PORT_STATS_MENU_NONE;
        break;
    case 2:
        menu_screen = PORT_STATS_MENU_EQUIP;
        break;
    case 3:
        menu_screen = PORT_STATS_MENU_STATUS;
        break;
    case 5:
        menu_screen = PORT_STATS_MENU_ARRANGE;
        break;
    }
}

/* ---- The item under the menu's cursor: what it does ----------------------------------------------- */
static const char *const ailment_words[6] = {"poison", "paralysis", "darkness", "curse", "slow", "magic seal"};

static void item_line(PortStatsMenu *m, const char *text) {
    if (text[0] != 0 && m->item_line_count < PORT_STATS_MENU_LINES) {
        snprintf(m->item_lines[m->item_line_count++], PORT_STATS_MENU_LINE, "%s", text);
    }
}

/* The ailments of a mask (bit n: timed_flags[n]) as words: "poison", "poison and curse", ... */
static void ailment_list(int mask, char *out, size_t size) {
    int i, count = 0, done = 0;
    size_t at = 0;

    out[0] = 0;
    for (i = 0; i < 6; i++) {
        count += (mask >> i) & 1;
    }
    for (i = 0; i < 6 && at < size; i++) {
        if (!((mask >> i) & 1)) {
            continue;
        }
        at += snprintf(out + at, size - at, "%s%s", done == 0 ? "" : done == count - 1 ? " and " : ", ", ailment_words[i]);
        done++;
    }
}

/* Player_UseItem, type 0: timed_flags[n] is cleared for each bit n of ItemCommonDef.timed_flags */
static void cure_text(int mask, char *out, size_t size) {
    char list[96];
    int count = 0, i;

    for (i = 0; i < 6; i++) {
        count += (mask >> i) & 1;
    }
    if (count == 6) {
        snprintf(out, size, "Cures every ailment");
    } else if (count >= 4) {
        ailment_list(~mask & 0x3F, list, sizeof(list));
        snprintf(out, size, "Cures every ailment but %s", list);
    } else {
        ailment_list(mask, list, sizeof(list));
        snprintf(out, size, "Cures %s", list);
    }
}

/* The HP and MP a type 0 item gives back; 999 or more is all of it (Player_RecomputeStats caps hp_max and
   mp_max at 999) */
static void restore_text(int hp, int mp, char *out, size_t size) {
    out[0] = 0;
    if (hp >= 999 && mp >= 999) {
        snprintf(out, size, "Restores all HP and MP");
    } else if (hp > 0 && mp > 0) {
        snprintf(out, size, "Restores %d HP and %d MP", hp, mp);
    } else if (hp >= 999 || mp >= 999) {
        snprintf(out, size, "Restores all %s", hp ? "HP" : "MP");
    } else if (hp > 0 || mp > 0) {
        snprintf(out, size, "Restores %d %s", hp ? hp : mp, hp ? "HP" : "MP");
    }
}

/* The crystals (Player_ApplyItem): Player_LearnSpellInRow teaches the first spell of the school not yet
   known, and mp_base rises */
static void crystal_text(int school, int mp_gain, char *out, size_t size) {
    char name[PORT_STATS_NAME];
    int slot;

    for (slot = 0; slot < 5 && spell_levels[school][slot] != 0; slot++) {
    }
    if (slot < 5) {
        /* the spell of school r, slot c at level 1 is effect (r * 5 + c) * 3 + 1 (Player_CountMagicUse) */
        spell_name((school * 5 + slot) * 3 + 1, name, sizeof(name));
        snprintf(out, size, "Teaches %s; most MP +%d", name, mp_gain);
    } else {
        snprintf(out, size, "Most MP +%d (its spells are all known)", mp_gain);
    }
}

/* A carried item (below ITEM_WEAPON_FIRST): Player_UseItem and Player_ApplyItem, by its row of
   param/item_cmn.prm (item_common_defs) */
static void describe_carried(PortStatsMenu *m, int item) {
    const ItemCommonDef *d = &item_common_defs[item];
    char text[PORT_STATS_MENU_LINE], name[PORT_STATS_NAME];
    int i, shooters = 0;

    switch (d->type) {
    case 0:
        if (item >= ITEM_ARROW && item <= ITEM_WIDDAS_ARROW) {
            /* the menu does not use them (Menu_UpdateUseItem); a weapon shoots while there is one of its
               PlayerItemDef.ammo_item_index (Player_UpdateActions) */
            name[0] = 0;
            for (i = 0; i < ITEM_WORN_FIRST - ITEM_WEAPON_FIRST; i++) {
                if (weapon_defs[i].ammo_item_index == item && shooters++ == 0) {
                    item_name(ITEM_WEAPON_FIRST + i, name, sizeof(name));
                }
            }
            if (shooters > 1) {
                snprintf(text, sizeof(text), "Shot by %s and %d more", name, shooters - 1);
            } else if (shooters == 1) {
                snprintf(text, sizeof(text), "Shot by %s", name);
            } else {
                snprintf(text, sizeof(text), "Ammunition");
            }
            item_line(m, text);
            return;
        }
        text[0] = 0;
        switch (item) {
        case ITEM_POWER_SEED: /* power_seed_timer 1800 frames: Physical Power + a tenth (Player_RecomputeStats) */
            snprintf(text, sizeof(text), "Physical Power +10%% for 60 s");
            break;
        case ITEM_WIDDA_NEEDLE: /* widda_needle_timer, the same for Magic Power */
            snprintf(text, sizeof(text), "Magic Power +10%% for 60 s");
            break;
        case ITEM_WISE_MANS_FRUIT: /* magic_base + 1, up to 999 */
            snprintf(text, sizeof(text), "%s",
                     player_state.magic_base < 999 ? "Magic Power +1 for good" : "Magic Power is at its most (999)");
            break;
        case ITEM_FOOLS_FRUIT: /* power_base + 1, up to 999 */
            snprintf(text, sizeof(text), "%s",
                     player_state.power_base < 999 ? "Physical Power +1 for good" : "Physical Power is at its most (999)");
            break;
        case ITEM_MAGIC_CRYSTAL: /* mp_base + 1 */
            snprintf(text, sizeof(text), "Most MP +1");
            break;
        default:
            if (item >= ITEM_FIRE_CRYSTAL && item <= ITEM_DARK_CRYSTAL) { /* mp_base + 1 */
                crystal_text(item - ITEM_FIRE_CRYSTAL, 1, text, sizeof(text));
            } else if (item >= ITEM_FIRE_CRYSTAL_2 && item <= ITEM_DARK_CRYSTAL_2) { /* mp_base + 5 */
                crystal_text(item - ITEM_FIRE_CRYSTAL_2, 5, text, sizeof(text));
            }
            break;
        }
        item_line(m, text);
        restore_text(d->hp_heal, d->mp_heal, text, sizeof(text));
        item_line(m, text);
        if (d->timed_flags & 0x3F) {
            cure_text(d->timed_flags & 0x3F, text, sizeof(text));
            item_line(m, text);
        }
        if (item == 0x12) { /* Player_ApplyPoisonWater: timed_flags[0] 1800, whatever the defence */
            item_line(m, "Poisons you for 60 s");
        }
        if (d->use_mode == 2) { /* one of ItemCommonDef.item is given back */
            item_name(d->item, name, sizeof(name));
            snprintf(text, sizeof(text), "Leaves a %s", name);
            item_line(m, text);
        }
        if (m->item_line_count == 0) {
            item_line(m, "No effect when used");
        }
        return;
    case 1:
    case 4:
    case 5: /* MapObject_UseItem, on the object in front of the player */
        item_line(m, d->use_mode == 0 ? "Use it facing what it is for (used up)" : "Use it facing what it is for");
        return;
    case 2: /* MapChange_StartPassage, standing on a symbol of passage */
        item_line(m, "Use it standing on a symbol of passage");
        return;
    case 6: /* MapViewer_OpenForItem */
        item_line(m, "Shows its map");
        return;
    case 8: /* "Nothing happened" */
        if (item == ITEM_EARTH_STONE) { /* Smith_UpdateListWithConfirm takes one for each upgrade */
            item_line(m, "The smith takes one for each upgrade");
        } else if (item == ITEM_BONE || item == ITEM_SCALE || item == ITEM_CLAW) {
            /* the trades paid in them (Trade_UpdateTakeAmount: LevelDef.upgrade_material_kind 1 to 3) */
            item_line(m, "Some trades are paid with these");
        } else {
            item_line(m, "Nothing happens when used");
        }
        return;
    }
}

/* A weapon or a worn item: its level and wear, the ailments it guards against, its data bonuses and what
   the code does for it */
static void describe_equipment(PortStatsMenu *m, int item) {
    PortStatsSnapshot *bonuses;
    const LevelRecord *record;
    char text[PORT_STATS_MENU_LINE], list[96];
    int i, mask = 0, defence = 0, wears;
    static PortStatsSnapshot scratch; /* only its effects are used */

    bonuses = &scratch;
    bonuses->effect_count = 0;
    if (item >= ITEM_WORN_FIRST) {
        int index = ITEM_WORN_INDEX(item);

        if (item == ITEM_SHIELD_NONE || item == ITEM_HELM_NONE || item == ITEM_ARMOR_NONE || item == ITEM_GAUNTLET_NONE ||
            item == ITEM_BOOT_NONE || item == ITEM_RING_NONE || item == ITEM_BRACELET_NONE || item == ITEM_AMULET_NONE) {
            return;
        }
        record = &worn_item_level_records[index];
        wears = item < ITEM_RING_NONE; /* Attack_WearArmorFromHit: shield, helm, armour, gloves, boots */
        for (i = 0; i < 6; i++) {
            int rate = worn_defs[index].worn_ailment_defence[i];

            if (rate > 0) {
                mask |= 1 << i;
                defence = defence == 0 || defence == rate ? rate : -1;
            }
        }
        bonus_effects(bonuses, item, worn_defs[index].bonus_kinds, worn_defs[index].bonus_amounts);
    } else {
        int index = ITEM_WEAPON_INDEX(item);

        if (index == 0) {
            return;
        }
        record = &weapon_level_records[index];
        wears = weapon_defs[index].action_handler_index < 2; /* Attack_WearWeapon: not the bows */
        /* Player_CountWeaponUse: level 2 at 180 hits, 3 at 480 */
        if (record->level_index >= 2) {
            snprintf(text, sizeof(text), "Weapon level 3");
        } else {
            snprintf(text, sizeof(text), "Weapon level %d: %d / %d hits", record->level_index + 1, (u16)record->use_count,
                     record->level_index == 0 ? 180 : 480);
        }
        item_line(m, text);
        bonus_effects(bonuses, item, weapon_defs[index].bonus_kinds, weapon_defs[index].bonus_amounts);
    }
    if (wears && port_stats_options.durability) {
        /* LevelRecord.level: the percent its values count at, worn down to 50 */
        snprintf(text, sizeof(text), "Durability %d%%", record->level);
        item_line(m, text);
    }
    if (mask != 0) {
        /* Player_AddItemBonuses: worn_ailment_defence, the sum capped at 100 (Player_RecomputeStats) */
        ailment_list(mask, list, sizeof(list));
        if (defence > 0) {
            snprintf(text, sizeof(text), "Guards against %s %d%%", list, defence > 100 ? 100 : defence);
        } else {
            snprintf(text, sizeof(text), "Guards against %s", list);
        }
        item_line(m, text);
    }
    for (i = 0; i < bonuses->effect_count; i++) {
        item_line(m, bonuses->effects[i].text);
    }
    if (item_code_effect(item, text, sizeof(text))) {
        item_line(m, text);
    }
    if (ancient_piece(item)) { /* Player_RecomputeStats, Player_Update */
        item_line(m, item == ITEM_ANCIENT_SHIELD ? "Whole Ancient set: its weight counts a third; without it, no running"
                                                 : "Whole Ancient set: its weight counts a third");
    }
}

/* How far a screen has faded in: its parts all come and go together, by a tenth of their alpha a frame,
   so screen_part_alphas[0] against its part's alpha is it */
static float screen_fade(const SpritePacket *parts, u32 count) {
    float fade;

    if (parts == NULL || count == 0 || parts[0].alpha == 0) {
        return 1.0f;
    }
    fade = screen_part_alphas[0] / (float)parts[0].alpha;
    return fade < 0.0f ? 0.0f : fade > 1.0f ? 1.0f : fade;
}

/* The ailment icons beside Condition: the parts of MenuSprite_ShowIfFlagSet (handler 23), each shown while
   the ailment of its slot runs; MenuSprite_Draw puts a part at X - 1728, (Y - 1936) * 2 of the menus'
   640 x 448 picture (their XYOFFSET, the field's rows doubled) */
static void ailment_icons(PortStatsMenu *m, const SpritePacket *parts, u32 count) {
    static const int ailment_of_slot[6] = {2, 1, 3, 0, 4, 5};
    u32 i;

    for (i = 0; parts != NULL && i < count && m->icon_count < 6; i++) {
        const SpritePacket *p = &parts[i];
        float x0 = (float)p->xyz0.X / 16.0f - 1728.0f, x1 = (float)p->xyz1.X / 16.0f - 1728.0f;
        float y0 = ((float)p->xyz0.Y / 16.0f - 1936.0f) * 2.0f, y1 = ((float)p->xyz1.Y / 16.0f - 1936.0f) * 2.0f;
        float *icon;

        if (p->handler_index != 23 || p->slot < 0 || p->slot >= 6 ||
            (u16)player_state.timed_flags[ailment_of_slot[(int)p->slot]] == 0) {
            continue;
        }
        icon = m->icons[m->icon_count++];
        icon[0] = x0 < x1 ? x0 : x1;
        icon[1] = y0 < y1 ? y0 : y1;
        icon[2] = x0 < x1 ? x1 : x0;
        icon[3] = y0 < y1 ? y1 : y0;
    }
}

static void sample_menu(PortStatsMenu *m) {
    const MenuItem *cursor = NULL;
    int i;

    m->screen = menu_screen;
    m->item = -1;
    switch (menu_screen) {
    case PORT_STATS_MENU_MAIN:
        m->shown = screen_fade(main_parts, main_part_count);
        break;
    case PORT_STATS_MENU_STATUS:
        m->shown = screen_fade(status_parts, status_part_count);
        break;
    case PORT_STATS_MENU_EQUIP:
        m->shown = screen_fade(equip_parts, equip_part_count);
        m->covered = detail_panel_flags.bytes.detail_panel_open != 0 || detail_panel_flags.bytes.equipped_list_open != 0;
        break;
    case PORT_STATS_MENU_ITEMS:
        m->shown = screen_fade(item_parts, item_part_count);
        cursor = item_list_cursor[0];
        break;
    case PORT_STATS_MENU_ARRANGE:
        m->shown = screen_fade(recol_parts, recol_part_count);
        m->covered = detail_panel_flags.bytes.detail_panel_open != 0;
        cursor = owned_list_current[0];
        break;
    }
    if (menu_screen == PORT_STATS_MENU_MAIN) {
        ailment_icons(m, main_parts, main_part_count);
    } else if (menu_screen == PORT_STATS_MENU_STATUS) {
        ailment_icons(m, status_parts, status_part_count);
    }
    if (cursor != NULL && cursor->item_number >= 0 && cursor->item_number < ITEM_COUNT) {
        m->item = cursor->item_number;
        if (m->item < ITEM_WEAPON_FIRST) {
            describe_carried(m, m->item);
        } else {
            describe_equipment(m, m->item);
        }
    }
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
