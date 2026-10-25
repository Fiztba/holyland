/* weapon_sets.c: PC-only. The weapon sets mod's game side (port/include/port/weapon_sets.h,
   docs/port/WEAPON_SETS.md): a second weapon and shield ("set 2") kept beside the ones in hand, swapped with
   them on one key, and chosen on the equipment screen as two more rows above the weapon's.

   The equipment screen (Menu_UpdateEquip) shows one row at a time: a MenuItemList, whose `item` is the cursor
   in that row's ring of owned items (Menu_BuildItemRings); Up and Down walk the ring of rows (`next` up,
   `prev` down: Weapon, Magic, Shield, Helm ... Shortcut, Weapon again), Left and Right the items. Weapon 2
   and Shield 2 are two more MenuItemLists over the same rings of items (MenuItem.group 0 and 2), linked
   between Shortcut and Weapon, so that Up from Weapon reaches them and the armour stays as far down as
   ever. Everything the screen does with a row (the item's model and its neighbours', the detail panel, the
   cursor) follows the row's `item` and `group`, so the rows work as the game's own; four lines of
   ui/menu_screens.c (in patches/) hand the rest here: linking them (WeaponSets_LinkLists), the row's
   number (WeaponSets_TrackEquip: equip_group_shown is the row's group, as it is for every game row), the
   "E" of the item kept (WeaponSets_ShownIsEquipped) and Cross, which keeps the item for set 2 instead of
   wearing it (WeaponSets_Choose).

   The swap (WeaponSets_Poll, once a game frame from Quick_PollGame, after GameLoop_OpenRequestedMenu): the
   press is taken only where the game would let its menu open (Player_Update's menu button:
   the player in control, not dying, no item being used; and GameLoop_OpenRequestedMenu's waits for the
   files and sounds), and not while the weapon is out (mid-swing, a bow drawn). Then it does what Cross on
   the equipment screen does for the weapon and for the shield (a weapon not held in one hand leaves no
   shield), Player_RecomputeStats and the HP and MP caps, and what the menu's closing does
   (GameLoop_OpenRequestedMenu): Player_LoadArmModels (the held weapon's model, motions and sounds), the
   strength gauge emptied and the weapon's magic charged when the weapon changed. Durability and the
   weapon's level are the item's own records (weapon_level_records, worn_item_level_records), so they
   follow the item. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "core/game.h"
#include "item/item.h"
#include "map/map.h"
#include "player/player.h"
#include "sound/sound.h"
#include "ui/menu.h"
#include "port/platform.h"
#include "port/weapon_sets.h"

extern GameStatus game_status;
extern PlayerState player_state;
extern u8 item_counts[ITEM_COUNT];
extern LevelDef item_name_defs[ITEM_COUNT];
extern LevelRecord weapon_level_records[70];
extern LevelRecord worn_item_level_records[160];
extern PlayerItemDef weapon_defs[];
extern MenuItem weapon_entries[70];
extern MenuItem shield_entries[30];
extern MenuItemList equip_weapon_list;
extern MenuItemList equip_shield_list;
extern MenuItemList equip_shortcut_list;
extern ColorBytes menu_draw_color;
extern DetailPanelFlags detail_panel_flags; /* detail_panel_open and equipped_list_open */
extern SoundLoadQueue sound_load_queue;
extern MapLoadState map_load_state;
extern int context1_buffer_half;
s32 Sound_PlayMenu(s32);
void Player_RecomputeStats(void);
void Player_LoadArmModels(void);
int File_IsLocked(void);
int Sound_BgmLoadCanYield(void);
int Message_IsBoxOpen(void);
s8 MenuDraw_GetWordStart(s32 index);
const char *Quick_WhyNotInPlay(void); /* game/quicksave.c */

#define WEAPONS 70
#define SHIELDS 30
#define PRESS_FRAMES 60 /* a press waits this many vblanks at most for the files and sounds */

static MenuItemList weapon2_list; /* the rows Weapon 2 and Shield 2 */
static MenuItemList shield2_list;
static int linked;                /* they are in the ring of rows */

/* ---- What is owned ------------------------------------------------------------------------------- */

/* As Menu_BuildItemRings rings the items of a group: owned, usable and not with the smith */
static int weapon_owned(int weapon) {
    if (weapon == 0) {
        return 1;
    }
    return weapon > 0 && weapon < WEAPONS && item_counts[ITEM_WEAPON_FIRST + weapon] != 0 &&
           (item_name_defs[ITEM_WEAPON_FIRST + weapon].state & 1) && weapon_level_records[weapon].in_smith_rings == 0;
}

static int shield_owned(int shield) {
    if (shield == 0) {
        return 1;
    }
    return shield > 0 && shield < SHIELDS && item_counts[ITEM_WORN_FIRST + shield] != 0 &&
           (item_name_defs[ITEM_WORN_FIRST + shield].state & 1) && worn_item_level_records[shield].in_smith_rings == 0;
}

/* A one-handed weapon (PlayerItemDef.action_handler_index 0) leaves room for a shield; the others (two-handed
   ones, 1, and bows, 2) do not (Menu_UpdateEquip) */
static int one_handed(int weapon) {
    return weapon_defs[weapon].action_handler_index == 0;
}

/* Set 2 without what is no longer owned (sold, dropped, given to the smith) */
static void drop_unowned(void) {
    PortWeaponSet *set = &port_weapon_set2;

    if (!weapon_owned(set->weapon)) {
        port_event("weapon sets: set 2's weapon %d is no longer owned, dropped", set->weapon);
        set->weapon = 0;
        port_weapon_sets_changed();
    }
    if (!shield_owned(set->shield) || (set->shield != 0 && !one_handed(set->weapon))) {
        port_event("weapon sets: set 2's shield %d is no longer owned, dropped", set->shield);
        set->shield = 0;
        port_weapon_sets_changed();
    }
}

/* ---- Names, the game's own (param/item_nam.prm): the text code of docs GAME_DATA.md --------------------- */
static void item_name(int item, char *out, size_t size) {
    const u16 *text = (const u16 *)&item_name_defs[item];
    size_t at = 0;
    int i;

    for (i = 0; i < 0x30 / 2 && at + 1 < size; i++) {
        if (text[i] == 0xFFFF) {
            break;
        }
        if ((text[i] & 0xFF00) == 0x0800) {
            out[at++] = (char)((text[i] & 0xFF) + 0x20);
        } else if (text[i] == 0x8000) {
            out[at++] = ' ';
        } else if (text[i] != 0) {
            out[at++] = '?';
        }
    }
    out[at] = 0;
}

/* ---- The equipment screen's rows (ui/menu_screens.c) ----------------------------------------------- */

/* The item's node when it is in the row's ring of owned items, else the row's own cursor */
static MenuItem *in_ring(MenuItemList *row, MenuItem *wanted) {
    MenuItem *item = row->item;
    int i;

    for (i = 0; item != NULL && i < WEAPONS; i++, item = item->next) {
        if (item == wanted) {
            return wanted;
        }
    }
    return row->item;
}

/* Menu_BuildItemRings, as it ends: with the mod on, Weapon 2 and Shield 2 between Shortcut and Weapon, their
   cursors on set 2's items; with it off, the game's ring of rows as it was */
void WeaponSets_LinkLists(void) {
    if (!port_weapon_sets.enabled) {
        if (linked) {
            equip_weapon_list.next = &equip_shortcut_list;
            equip_shortcut_list.prev = &equip_weapon_list;
            linked = 0;
        }
        return;
    }
    drop_unowned();
    weapon2_list.group = 0;
    shield2_list.group = 2;
    weapon2_list.item = in_ring(&equip_weapon_list, &weapon_entries[port_weapon_set2.weapon]);
    shield2_list.item = in_ring(&equip_shield_list, &shield_entries[port_weapon_set2.shield]);
    if (!linked) {
        /* up (next) from Weapon: Shield 2, Weapon 2, then Shortcut as before */
        equip_weapon_list.next = &shield2_list;
        shield2_list.prev = &equip_weapon_list;
        shield2_list.next = &weapon2_list;
        weapon2_list.prev = &shield2_list;
        weapon2_list.next = &equip_shortcut_list;
        equip_shortcut_list.prev = &weapon2_list;
        linked = 1;
    }
}

/* Menu_UpdateEquip, each frame, once Up and Down are taken: equip_group_shown (which Menu_IsEquipped and
   Menu_BuildItemRings read) is the row's group, as it always is for the game's rows (equip_weapon_list.group
   0 ... equip_shortcut_list.group 10), whatever the extra rows did to the count. Notes the screen for the
   overlay. */
s32 WeaponSets_TrackEquip(MenuItemList *list, s32 group_shown) {
    PortWeaponSetsMenu *m = &port_weapon_sets_menu;
    s32 glyph;

    if (!linked) {
        return group_shown;
    }
    port_weapon_sets_take_press(); /* the key does nothing in the menu */
    m->vblank = port_vblank_count();
    m->row = list == &weapon2_list ? 1 : list == &shield2_list ? 2 : 0;
    m->group = list->group;
    m->alpha = menu_draw_color.alpha / 128.0f;
    /* the row's name is the word MenuDraw_Word draws, centred: equip_group_glyphs (0x5B "Weapon", 0x5D
       "Shield"), its width in cells of 20 */
    glyph = list->group == 2 ? 0x5D : 0x5B;
    m->label_half_width = (MenuDraw_GetWordStart(glyph + 1) - MenuDraw_GetWordStart(glyph) - 1) * 10.0f;
    m->covered = detail_panel_flags.bytes.detail_panel_open != 0 || detail_panel_flags.bytes.equipped_list_open != 0;
    m->empty = port_weapon_set2.weapon == 0 && port_weapon_set2.shield == 0;
    item_name(ITEM_WEAPON_FIRST + port_weapon_set2.weapon, m->weapon, sizeof(m->weapon));
    item_name(ITEM_WORN_FIRST + port_weapon_set2.shield, m->shield, sizeof(m->shield));
    return list->group;
}

/* The "E" by the item: on Weapon 2 and Shield 2, set 2's item; on the game's rows, `equipped` (Menu_IsEquipped) */
int WeaponSets_ShownIsEquipped(MenuItemList *list, MenuItem *item, int equipped) {
    if (list == &weapon2_list) {
        return item->item_number == ITEM_WEAPON_FIRST + port_weapon_set2.weapon;
    }
    if (list == &shield2_list) {
        return item->item_number == ITEM_WORN_FIRST + port_weapon_set2.shield;
    }
    return equipped;
}

/* Cross on Weapon 2 or Shield 2: the item is kept for set 2, by the equipment screen's own rules (a weapon not
   held in one hand clears the shield, a shield clears such a weapon), with the screen's sound. 0: one of the
   game's rows, which the game handles. */
int WeaponSets_Choose(MenuItemList *list, MenuItem *item) {
    PortWeaponSet *set = &port_weapon_set2;

    if (list == &weapon2_list) {
        if (item->item_number - ITEM_WEAPON_FIRST != set->weapon) {
            Sound_PlayMenu(0);
            set->weapon = item->item_number - ITEM_WEAPON_FIRST;
            if (!one_handed(set->weapon)) {
                set->shield = 0;
            }
            port_weapon_sets_changed();
            port_event("weapon sets: set 2 weapon %d, shield %d (chosen)", set->weapon, set->shield);
        }
        return 1;
    }
    if (list == &shield2_list) {
        if (item->item_number - ITEM_WORN_FIRST != set->shield) {
            Sound_PlayMenu(0);
            set->shield = item->item_number - ITEM_WORN_FIRST;
            if (!one_handed(set->weapon)) {
                set->weapon = 0;
            }
            port_weapon_sets_changed();
            port_event("weapon sets: set 2 weapon %d, shield %d (chosen)", set->weapon, set->shield);
        }
        return 1;
    }
    return 0;
}

/* ---- The swap ------------------------------------------------------------------------------------- */

/* Why the game would not take the press now (NULL: it would): the menu button's conditions (Player_Update,
   GameLoop_OpenRequestedMenu), and the weapon put away */
static const char *refused(void) {
    if (game_status.bit9) {
        return "the game is leaving play";
    }
    if (game_status.player_control == 0 || (game_status.update_hold_flags & 1)) {
        return "the player is not in control";
    }
    if (player_state.death_timer != 0) {
        return "the player is dying";
    }
    if (game_status.paused || Message_IsBoxOpen()) {
        return "the game is paused or a message is open";
    }
    if (game_status.bit6 || game_status.item_use_cooldown > 0) {
        return "an item is being used";
    }
    if (player_state.item_action_step != 0 || player_state.frames_since_item_action < 2) {
        return "the weapon is out (mid-swing)";
    }
    if (game_status.bit2 || game_status.bit3 || game_status.bit4 || game_status.save_requested || game_status.bit8 ||
        game_status.bit14) {
        return "a menu is opening";
    }
    return NULL;
}

/* GameLoop_OpenRequestedMenu's waits before a menu: 1 while the arm models could not be loaded now */
static int must_wait(void) {
    if (context1_buffer_half == 0) {
        return 1;
    }
    if (sound_load_queue.map_bank_load_pending != 0 || sound_load_queue.entity_bank_load_pending != 0) {
        return 1;
    }
    if (sound_load_queue.bgm_load_pending != 0 && Sound_BgmLoadCanYield() == 0) {
        return 1;
    }
    /* the menu holds a map load at steps 2 to 4 (MapLoad_RequestHold); the swap waits for it instead */
    if (map_load_state.step >= 2 && map_load_state.step <= 4) {
        return 1;
    }
    return File_IsLocked() != 0;
}

static void log_player(const char *what) {
    fprintf(stderr,
            "[weapon sets] vblank %llu: %s: weapon %d shield %d (set 2: weapon %d shield %d); power %d, attack %d %d %d, "
            "defence %d %d %d, weight %d / %d, level %d (%d hits), durability %d, arrows %d, power gauge %d, charging magic %d, shield durability %d\n",
            (unsigned long long)port_vblank_count(), what, player_state.weapon, player_state.equipment[EQUIP_SHIELD],
            port_weapon_set2.weapon, port_weapon_set2.shield, player_state.power, player_state.attack_stats.values[0],
            player_state.attack_stats.values[1], player_state.attack_stats.values[2],
            player_state.defence_stats.values[0], player_state.defence_stats.values[1],
            player_state.defence_stats.values[2], player_state.weight, player_state.max_weight,
            weapon_level_records[player_state.weapon].level_index + 1, weapon_level_records[player_state.weapon].use_count,
            weapon_level_records[player_state.weapon].level,
            weapon_defs[player_state.weapon].action_handler_index == 2 ? item_counts[weapon_defs[player_state.weapon].ammo_item_index] : -1,
            player_state.power_gauge, player_state.charging_magic,
            worn_item_level_records[player_state.equipment[EQUIP_SHIELD]].level);
}

static void swap(void) {
    PortWeaponSet *set = &port_weapon_set2;
    int old_weapon = player_state.weapon, old_shield = player_state.equipment[EQUIP_SHIELD];
    PlayerItemDef *def;
    char weapon[PORT_WEAPON_SETS_NAME], shield[PORT_WEAPON_SETS_NAME];

    drop_unowned();
    log_player("before the swap");
    /* the equipment screen's Cross (Menu_UpdateEquip): the weapon, then the shield; a weapon not held in one
       hand leaves no shield */
    player_state.weapon = set->weapon;
    player_state.equipment[EQUIP_SHIELD] = one_handed(set->weapon) ? set->shield : ITEM_WORN_INDEX(ITEM_SHIELD_NONE);
    set->weapon = old_weapon;
    set->shield = old_shield;
    port_weapon_sets_changed();
    Player_RecomputeStats();
    if (player_state.hp_max < player_state.hp_now) {
        player_state.hp_now = player_state.hp_max;
    }
    if (player_state.mp_now > player_state.mp_max) {
        player_state.mp_now = player_state.mp_max;
    }
    /* the menu's closing (GameLoop_OpenRequestedMenu) */
    Player_LoadArmModels();
    if (player_state.weapon != old_weapon) {
        player_state.power_gauge = 0;
        def = &weapon_defs[player_state.weapon];
        if (def->action_handler_index != 2) {
            if (def->first_magic_id != 0xFFFF) {
                player_state.magic_gauge = 0;
                player_state.charging_magic = def->first_magic_id;
            } else if (def->second_magic_id != 0xFFFF && weapon_level_records[player_state.weapon].level_index + 1 == 3) {
                player_state.magic_gauge = 0;
                player_state.charging_magic = def->second_magic_id;
            }
        }
    }
    log_player("swapped");
    item_name(ITEM_WEAPON_FIRST + player_state.weapon, weapon, sizeof(weapon));
    item_name(ITEM_WORN_FIRST + player_state.equipment[EQUIP_SHIELD], shield, sizeof(shield));
    if (player_state.equipment[EQUIP_SHIELD] != ITEM_WORN_INDEX(ITEM_SHIELD_NONE)) {
        snprintf(port_weapon_sets_swap.text, sizeof(port_weapon_sets_swap.text), "%s, %s", weapon, shield);
    } else {
        snprintf(port_weapon_sets_swap.text, sizeof(port_weapon_sets_swap.text), "%s", weapon);
    }
    port_weapon_sets_swap.vblank = port_vblank_count();
}

/* KF4_WEAPON_SETS_SWAP_AT=V,V,...: the key pressed at those vblanks (for tests, which play a pad script) */
static void test_presses(void) {
    static const char *next;
    static int read;
    unsigned long long at;
    char *end;

    if (!read) {
        read = 1;
        next = getenv("KF4_WEAPON_SETS_SWAP_AT");
    }
    if (next == NULL || *next == 0) {
        return;
    }
    at = strtoull(next, &end, 0);
    if (port_vblank_count() < at) {
        return;
    }
    next = *end == ',' ? end + 1 : NULL;
    fprintf(stderr, "[weapon sets] vblank %llu: the swap key (KF4_WEAPON_SETS_SWAP_AT)\n",
            (unsigned long long)port_vblank_count());
    port_weapon_sets_key();
}

/* Once a game frame, from Quick_PollGame */
void WeaponSets_Poll(void) {
    unsigned long long pressed, now = port_vblank_count();
    const char *why;

    if (Quick_WhyNotInPlay() == NULL && port_weapon_sets_new_game() && port_weapon_sets.enabled) {
        port_weapon_set2.weapon = port_weapon_set2.shield = 0;
        port_weapon_sets_changed();
        port_event("weapon sets: a new game, set 2 empty");
    }
    if (port_weapon_sets.enabled) {
        test_presses();
    }
    pressed = port_weapon_sets_pressed();
    if (pressed == 0) {
        return;
    }
    if (!port_weapon_sets.enabled) {
        port_weapon_sets_take_press();
        return;
    }
    why = refused();
    if (why == NULL && must_wait()) {
        if (now + 1 - pressed <= PRESS_FRAMES) {
            return;
        }
        why = "the files or sounds were busy too long";
    }
    port_weapon_sets_take_press();
    if (why != NULL) {
        fprintf(stderr, "[weapon sets] vblank %llu: the swap key ignored: %s (item_action_step %d, frames since %u)\n", now,
                why, player_state.item_action_step, (unsigned)player_state.frames_since_item_action);
        return;
    }
    swap();
}
