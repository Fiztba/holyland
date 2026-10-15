#include "kf_map.h"
#include "kf_message.h"
#include "kf_item.h"
#include "kf_game.h"
#include "kf_entity.h"
#include "kf_map_object.h"
#include "kf_player.h"
#include "kf_item_effect.h"

int Script_IsRunning(int);
int Script_Start(int, int);
void Player_ApplyItem(int);
void func_001BC968(int);
int func_001188E0(int);
void func_0011A438(int);
void func_001BA310(void);
void func_001BB5E8(void);
int func_001BC990(int);

extern u8 item_counts[];
extern ItemCommonDef D_0041A6F8[];
extern GameStatus D_0038CC50;
extern EventRunner D_00420D00;
extern PlayerState D_00413F80;
extern void *D_00414188[];
extern int D_00446770;
extern u32 D_00446ED4;
extern u8 spell_levels[6][5];

/* TRUE when the player cannot take one more of the item ("Can't carry any more", func_001C9FB0): a one-of-a-kind
   item already carried, or 99 of another. */
int Player_CannotCarryItem(int idx) {
    ItemCommonDef *entry;

    if (idx < 0) {
        return 0;
    }
    entry = D_0041A6F8 + idx;
    if (entry->flags >> 1 & 1) {
        if (item_counts[idx] != 0) {
            return 1;
        }
    } else {
        if (item_counts[idx] >= 0x63) {
            return 1;
        }
    }
    return 0;
}

void Menu_AddKeyItem(int); // match: one argument is passed (ScriptCmd_AddItem declares three)
/* Adds one of an item to what the player carries: up to 1 for weapons and worn items and for items with flag
   bit 1, up to 99 for the others ("Can't carry any more" past that). */
void Player_AddItem(int index) {
    ItemCommonDef *def;

    if (index >= ITEM_WORN_FIRST) {
        if (item_counts[index] == 0) {
            item_counts[index] = 1;
        }
    } else if (index >= ITEM_WEAPON_FIRST) {
        if (item_counts[index] == 0) {
            item_counts[index] = 1;
        }
    } else if (index >= 0) {
        def = &D_0041A6F8[index];
        if (def->flags >> 1 & 1) {
            if (item_counts[index] == 0) {
                item_counts[index] = 1;
                if (def->type == 3) {
                    Menu_AddKeyItem(index);
                }
            }
        } else if (item_counts[index] < 99) {
            item_counts[index]++;
            if (def->type == 3) {
                Menu_AddKeyItem(index);
            }
        } else {
            Message_ShowNotice(0, 0, 0, 0, 0x2D, MSG_CANT_CARRY_ANY_MORE);
        }
    }
}

/* Takes one from the count at the index, if there is any. */
void func_001BC2A0(u32 index) {
    if (index < 0x17C && item_counts[index] != 0) {
        item_counts[index]--;
    }
}

/* Uses the item GameStatus.unk20 names once GameStatus.bit6 asks for it: an entity or a map object in front of
   the player may take it for a script of its own; otherwise it does what its type says and one is used up. */
void Player_UseItem(void) {
    int item = D_0038CC50.unk20;
    ItemCommonDef *def = &D_0041A6F8[item];
    Entity *entity;
    MapObject *object;
    MapObject *stood_on;
    u16 timed;

    if (D_0038CC50.unk44 > 0) {
        D_0038CC50.unk44--;
    }
    if (D_0038CC50.bit6) {
        D_0038CC50.bit6 = FALSE;
        D_0038CC50.unk44 = 1;
        if (item_counts[item] != 0) {
            entity = D_00420D00.unk54;
            if (entity != NULL && entity->spawn_info->unk58 != 0 && entity->group_id == D_00446770) {
                if (entity->spawn_info->unk4E != 0 && Script_IsRunning(entity->spawn_info->unk4E) != 0) {
                    return;
                }
                Script_Start(((Entity *)D_00420D00.unk54)->spawn_info->unk58, 0);
                return;
            }
            object = D_00420D00.current;
            if (object != NULL && object->unk08->unk58 != 0 && (MAP_OBJECT_BYTES(object)->unk00 & 1) == D_00446770) {
                if (object->unk08->unk40 != 0 && Script_IsRunning(object->unk08->unk40) != 0) {
                    return;
                }
                Script_Start(((MapObject *)D_00420D00.current)->unk08->unk58, 0);
                return;
            }
            Player_ApplyItem(item);
            switch (def->type) {
            case 0:
                D_00413F80.hp_now += def->hp_heal;
                if (D_00413F80.hp_now > D_00413F80.hp_max) {
                    D_00413F80.hp_now = D_00413F80.hp_max;
                }
                D_00413F80.mp_now += def->mp_heal;
                if (D_00413F80.mp_now > D_00413F80.mp_max) {
                    D_00413F80.mp_now = D_00413F80.mp_max;
                }
                timed = def->unk08;
                if (timed & 1) {
                    D_00413F80.timed_flags[0] = 0;
                    D_00413F80.flag_timers[0] = 60;
                }
                if (timed & 2) {
                    D_00413F80.timed_flags[1] = 0;
                    D_00413F80.flag_timers[1] = 60;
                }
                if (timed & 4) {
                    D_00413F80.timed_flags[2] = 0;
                    D_00413F80.flag_timers[2] = 60;
                }
                if (timed & 8) {
                    D_00413F80.timed_flags[3] = 0;
                    D_00413F80.flag_timers[3] = 60;
                }
                if (timed & 0x10) {
                    D_00413F80.timed_flags[4] = 0;
                    D_00413F80.flag_timers[4] = 60;
                }
                if (timed & 0x20) {
                    D_00413F80.timed_flags[5] = 0;
                    D_00413F80.flag_timers[5] = 60;
                }
                break;
            case 1:
            case 4:
            case 5:
                if (D_00420D00.current == NULL || !MapObject_UseItem(D_00420D00.current, item)) {
                    Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_NOTHING_HAPPENED);
                    return;
                }
                break;
            case 2:
                stood_on = D_00414188[0];
                if (stood_on == NULL || stood_on->unk04->kind != MAP_OBJECT_KIND_SYMBOL_OF_PASSAGE || !func_001188E0(item)) {
                    Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_NOTHING_HAPPENED);
                    return;
                }
                break;
            case 3:
                return;
            case 6:
                func_0011A438(item);
                break;
            case 7:
                break;
            case 8:
                Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_NOTHING_HAPPENED);
                return;
            default:
                return;
            }
            func_001BC968(item);
            switch (def->use_mode) {
            case 0:
                item_counts[item]--;
                break;
            case 1:
                break;
            case 2:
                item_counts[item]--;
                item_counts[def->unk02]++;
                break;
            }
        }
    }
}

/* What using item `kind` does to the player itself: the Power Seed and the Widda Needle start a timer, the
   fruits raise unk2C ("Magic Power UP") or unk28 ("Physical UP"), the crystals raise unk24 and teach a magic. */
void Player_ApplyItem(int kind) {
    int marked;

    switch (kind) {
    case ITEM_POWER_SEED:
        D_00413F80.power_seed_timer = 1800;
        break;
    case ITEM_WIDDA_NEEDLE:
        D_00413F80.widda_needle_timer = 1800;
        break;
    case ITEM_WISE_MANS_FRUIT:
        if (D_00413F80.magic_base < 999) {
            Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_MAGIC_POWER_UP);
            D_00413F80.magic_base++;
        } else {
            Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_NOTHING_HAPPENED);
        }
        break;
    case ITEM_FOOLS_FRUIT:
        if (D_00413F80.power_base < 999) {
            D_00413F80.power_base++;
            Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_PHYSICAL_UP);
        } else {
            Message_ShowNotice(0, 0, 0, 1, 0x2D, MSG_NOTHING_HAPPENED);
        }
        break;
    case ITEM_MAGIC_CRYSTAL:
        D_00413F80.mp_base++;
        if (D_00413F80.mp_base > 999) {
            D_00413F80.mp_base = 999;
        }
        func_001BA310();
        ItemEffect_Start(kind, 0);
        break;
    case ITEM_FIRE_CRYSTAL:
    case ITEM_EARTH_CRYSTAL:
    case ITEM_WIND_CRYSTAL:
    case ITEM_WATER_CRYSTAL:
    case ITEM_LIGHT_CRYSTAL:
    case ITEM_DARK_CRYSTAL:
        marked = func_001BC990(kind - ITEM_FIRE_CRYSTAL);
        D_00413F80.mp_base++;
        if (D_00413F80.mp_base > 999) {
            D_00413F80.mp_base = 999;
        }
        func_001BA310();
        if (D_00413F80.unk264 == 0) {
            func_001BB5E8();
        }
        ItemEffect_Start(kind, marked);
        break;
    case ITEM_FIRE_CRYSTAL_2:
    case ITEM_EARTH_CRYSTAL_2:
    case ITEM_WIND_CRYSTAL_2:
    case ITEM_WATER_CRYSTAL_2:
    case ITEM_LIGHT_CRYSTAL_2:
    case ITEM_DARK_CRYSTAL_2:
        marked = func_001BC990(kind - ITEM_FIRE_CRYSTAL_2);
        D_00413F80.mp_base += 5;
        if (D_00413F80.mp_base > 999) {
            D_00413F80.mp_base = 999;
        }
        func_001BA310();
        if (D_00413F80.unk264 == 0) {
            func_001BB5E8();
        }
        ItemEffect_Start(kind, marked);
        break;
    }
}

/* Kind 0x12 starts the first timed flag: 1800 frames from now. */
void func_001BC968(int kind) {
    if (kind == 0x12) {
        D_00413F80.timed_flags[0] = 1800;
        D_00413F80.unk2C4 = D_00446ED4;
    }
}

/* Marks the first free one of the row's five slots. FALSE if all are taken. */
int func_001BC990(int row) {
    int i;

    for (i = 0; i < 5; i++) {
        if (spell_levels[row][i] == 0) {
            spell_levels[row][i] = 1;
            return TRUE;
        }
    }
    return FALSE;
}
