/* cheats.c: PC-only. The game's side of the debug cheats (port/include/port/cheats.h), taken in play at
   the end of a frame (Quick_PollGame), as the game itself would change these.

   - Spells: spell_levels[school][slot] is 0 for a spell not known, else its level (1 to 3, each its own
     spell in fx_table, with its own strength, effect and length; port_cheat_spell_level picks), and
     spell_experience counts toward the next (50 and 150: Player_CountMagicUse). A known spell is in the
     menus' lists, which are built as a menu opens. The gauge charges for PlayerState.charging_magic,
     picked as a crystal teaches a spell (Player_ApplyItem, Player_PickChargingMagic).
   - Items: item_counts is a count per item. 150 to 219 are the weapons and 220 to 379 the worn items,
     one each (Game_NormalizeStartState allows no more); below 150 the carried items, up to 99, or 1 of
     a one-of-a-kind item (ItemCommonDef.flags bit 1, as Player_AddItem has it). Items whose LevelDef
     state has bit 1 are the ones the menus never list, and are left alone; so are the keys (type 3),
     which open doors and belong to the story.
   - Magic: mp_base and magic_base are what Player_RecomputeStats makes mp_max and magic from (with what
     is worn); magic sets how fast the magic gauge fills (Player_UpdateStatus). 999 is the game's own
     cap (the crystals and fruits). */
#include <stdio.h>

#include "common.h"
#include "item/item.h"
#include "core/game.h"
#include "map/map.h"
#include "player/player.h"
#include "port/cheats.h"
#include "port/platform.h"

#define SPELL_SCHOOLS 6
#define SPELL_SLOTS 5
#define ITEM_KEY_TYPE 3
#define STAT_CAP 999

extern PlayerState player_state;
extern u8 spell_levels[SPELL_SCHOOLS][SPELL_SLOTS];
extern u16 spell_experience[SPELL_SCHOOLS][SPELL_SLOTS];
extern u8 item_counts[];
extern ItemCommonDef item_common_defs[];
extern GameDefinition item_name_defs[];

void Player_RecomputeStats(void);
void Player_PickChargingMagic(void);
const char *Quick_WhyNotInPlay(void);

/* An item the menus never list (LevelDef.state bit 1) */
static int unlisted(int item) {
    return (((LevelDef *)item_name_defs)[item].state & 2) != 0;
}

static int learn_spells(void) {
    static const u16 experience[4] = {0, 0, 50, 150}; /* where each level starts (Player_CountMagicUse) */
    int school, slot, learned = 0, level = port_cheat_spell_level();

    for (school = 0; school < SPELL_SCHOOLS; school++) {
        for (slot = 0; slot < SPELL_SLOTS; slot++) {
            learned += spell_levels[school][slot] != level;
            spell_levels[school][slot] = (u8)level;
            spell_experience[school][slot] = experience[level];
        }
    }
    if (player_state.charging_magic == 0) {
        Player_PickChargingMagic();
    }
    return learned;
}

static int add_equipment(void) {
    int item, added = 0;

    for (item = ITEM_WEAPON_FIRST; item < ITEM_COUNT; item++) {
        if (!unlisted(item) && item_counts[item] == 0) {
            item_counts[item] = 1;
            added++;
        }
    }
    return added;
}

static int add_items(void) {
    int item, added = 0;

    for (item = 1; item < ITEM_WEAPON_FIRST; item++) {
        const ItemCommonDef *def = &item_common_defs[item];
        int most = (def->flags >> 1) & 1 ? 1 : 99;

        if (def->type == ITEM_KEY_TYPE || unlisted(item)) {
            continue;
        }
        if (item_counts[item] < most) {
            item_counts[item] = (u8)most;
            added++;
        }
    }
    return added;
}

static void max_magic(void) {
    player_state.mp_base = STAT_CAP;
    player_state.magic_base = STAT_CAP;
    Player_RecomputeStats();
    player_state.mp_now = player_state.mp_max;
}

void Cheat_Poll(void) {
    char text[160];
    int cheats, n;
    const char *why;
    size_t at = 0;

    if (port_cheat_pending() == 0) {
        return;
    }
    why = Quick_WhyNotInPlay();
    if (why != NULL) {
        snprintf(text, sizeof(text), "waiting: %s", why);
        port_cheat_set_message(text);
        return;
    }
    cheats = port_cheat_take();
    text[0] = 0;
    if (cheats & PORT_CHEAT_SPELLS) {
        n = learn_spells();
        at += (size_t)snprintf(text + at, sizeof(text) - at, "%d spells set to level %d; ", n,
                               port_cheat_spell_level());
    }
    if (cheats & PORT_CHEAT_EQUIPMENT) {
        n = add_equipment();
        at += (size_t)snprintf(text + at, sizeof(text) - at, "%d pieces of equipment added; ", n);
    }
    if (cheats & PORT_CHEAT_ITEMS) {
        n = add_items();
        at += (size_t)snprintf(text + at, sizeof(text) - at, "%d kinds of item filled; ", n);
    }
    if (cheats & PORT_CHEAT_MAGIC) {
        max_magic();
        at += (size_t)snprintf(text + at, sizeof(text) - at, "MP %d, Magic Power %d; ", player_state.mp_max,
                               player_state.magic);
    }
    if (at >= 2 && at < sizeof(text)) {
        text[at - 2] = 0; /* the last "; " */
    }
    port_event("cheat: %s", text);
    port_cheat_set_message(text);
}
