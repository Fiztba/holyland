/* bss.c: the game's data from the decomp's bss.bss.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/game.h"
#include "core/load.h"
#include "core/state.h"
#include "ui/link.h"
#include "ui/menu.h"

/* 0x005CE0E0 */
char D_005CE0E0[1472] __attribute__((aligned(16)));

/* 0x005CE6A0 */
MenuItemList equipped_shortcut __attribute__((aligned(16)));

/* 0x005CE6B0 */
MenuItemList equipped_weapon __attribute__((aligned(16)));

/* 0x005CE6C0 */
MenuItemList equipped_shield __attribute__((aligned(16)));

/* 0x005CE6D0 */
MenuItemList equipped_helm __attribute__((aligned(16)));

/* 0x005CE6E0 */
MenuItemList equipped_armor __attribute__((aligned(16)));

/* 0x005CE6F0 */
MenuItemList equipped_gauntlet __attribute__((aligned(16)));

/* 0x005CE700 */
MenuItemList equipped_boot __attribute__((aligned(16)));

/* 0x005CE710 */
MenuItemList equipped_ring __attribute__((aligned(16)));

/* 0x005CE720 */
MenuItemList equipped_bracelet __attribute__((aligned(16)));

/* 0x005CE730 */
MenuItemList equipped_amulet __attribute__((aligned(16)));

/* 0x005CE740 */
MenuItemList equipped_spell __attribute__((aligned(16)));

/* 0x005CE760 */
MenuItemList use_item_list __attribute__((aligned(16)));

/* 0x005CE770 */
MenuItemList equip_weapon_list __attribute__((aligned(16)));

/* 0x005CE780 */
MenuItemList equip_shield_list __attribute__((aligned(16)));

/* 0x005CE790 */
MenuItemList equip_helm_list __attribute__((aligned(16)));

/* 0x005CE7A0 */
MenuItemList equip_armor_list __attribute__((aligned(16)));

/* 0x005CE7B0 */
MenuItemList equip_gauntlet_list __attribute__((aligned(16)));

/* 0x005CE7C0 */
MenuItemList equip_boot_list __attribute__((aligned(16)));

/* 0x005CE7D0 */
MenuItemList equip_ring_list __attribute__((aligned(16)));

/* 0x005CE7E0 */
MenuItemList equip_bracelet_list __attribute__((aligned(16)));

/* 0x005CE7F0 */
MenuItemList equip_amulet_list __attribute__((aligned(16)));

/* 0x005CE800 */
MenuItemList equip_spell_list __attribute__((aligned(16)));

/* 0x005CE810 */
MenuItemList equip_shortcut_list __attribute__((aligned(16)));

/* 0x005CE830 */
MenuItem *castable_spell_first[4] __attribute__((aligned(16)));

/* 0x005CE840 */
MenuItemList *equip_list_shown[4] __attribute__((aligned(16)));

/* 0x005CE850 */
MenuItem *owned_list_current[4] __attribute__((aligned(16)));

/* 0x005CE860 */
MenuItem *owned_list_top[4] __attribute__((aligned(16)));

/* 0x005CE870 */
MenuItem *owned_list_page_mark[4] __attribute__((aligned(16)));

/* 0x005CE880 */
MenuItem *castable_spell_cursor[4] __attribute__((aligned(16)));

/* 0x005CE890 */
MenuItem *castable_spell_top[4] __attribute__((aligned(16)));

/* 0x005CE8A0 */
MenuItem *castable_spell_page_mark[4] __attribute__((aligned(16)));

/* 0x005CE8B0 */
LinkNode model_load_spare __attribute__((aligned(16)));

/* 0x005CE900 */
ChunkPoolNode load_buffer_nodes[26] __attribute__((aligned(16)));

/* 0x005CE9D0. The decomp's labels weapon_model_name lie inside it (weapon_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode weapon_model_load __attribute__((aligned(16)));

/* 0x005CEA20: type from equip_model_loads, which points at it */
LinkNode D_005CEA20 __attribute__((aligned(16)));

/* 0x005CEA70. The decomp's labels shield_model_name lie inside it (shield_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode shield_model_load __attribute__((aligned(16)));

/* 0x005CEAC0. The decomp's labels armor_model_name lie inside it (armor_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode armor_model_load __attribute__((aligned(16)));

/* 0x005CEB10. The decomp's labels helm_model_name lie inside it (helm_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode helm_model_load __attribute__((aligned(16)));

/* 0x005CEB60. The decomp's labels gauntlet_model_name lie inside it (gauntlet_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode gauntlet_model_load __attribute__((aligned(16)));

/* 0x005CEBB0. The decomp's labels boot_model_name lie inside it (boot_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode boot_model_load __attribute__((aligned(16)));

/* 0x005CEC00. The decomp's labels ring_model_name lie inside it (ring_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode ring_model_load __attribute__((aligned(16)));

/* 0x005CEC50. The decomp's labels bracelet_model_name lie inside it (bracelet_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode bracelet_model_load __attribute__((aligned(16)));

/* 0x005CECA0. The decomp's labels amulet_model_name lie inside it (amulet_model_load.name[0]): the code that names them must use these
   fields, so they are not defined on their own */
LinkNode amulet_model_load __attribute__((aligned(16)));

/* 0x005CED40 */
LinkNode worn_model_loader __attribute__((aligned(16)));

/* 0x005CED90: type from pool_part_sources, which points at it */
PoolPartSource D_005CED90;

/* 0x005CEDA0: type from pool_part_sources, which points at it */
PoolPartSource D_005CEDA0;

/* 0x005CEDB0: type from pool_part_sources, which points at it */
PoolPartSource D_005CEDB0;

/* 0x005CEDC0: type from pool_part_sources, which points at it */
PoolPartSource D_005CEDC0;

/* 0x005CEDD0: type from pool_part_sources, which points at it */
PoolPartSource D_005CEDD0;

/* 0x005CEDE0: type from pool_part_sources, which points at it */
PoolPartSource D_005CEDE0;

/* 0x005CEDF0: type from pool_part_sources, which points at it */
PoolPartSource D_005CEDF0;

/* 0x005CEE00: type from pool_part_sources, which points at it */
PoolPartSource D_005CEE00;

/* 0x005CEE10: type from pool_part_sources, which points at it */
PoolPartSource D_005CEE10;

/* 0x005CEE40: type from pool_part_sources, which points at it */
PoolPartSource D_005CEE40;

/* 0x005CEE50: type from pool_part_sources, which points at it */
PoolPartSource D_005CEE50;

/* 0x005CEE60 */
MenuModelMotion *equip_weapon_motions[32] __attribute__((aligned(16)));

/* 0x005CEEE0 */
ChunkUpload npc_package_load __attribute__((aligned(16)));

/* 0x005CEF30 */
LinkNode key_item_model_loads[12] __attribute__((aligned(16)));

/* 0x005CF2F0 */
MenuScreenState card_screen_state;

/* 0x005CF318. On the PS2 its last element ([12]) lies over the first two bytes of trade_context, which
   follows it; here they are separate */
u16 button_settings_copy[13];

/* 0x005CF330 */
HandlerContext trade_context;

/* 0x005CF340 */
HandlerContext smith_context;

/* 0x005CF350 */
HandlerContext save_screen_context;

/* 0x005CF360 */
MenuItem *trade_stock_list_top[4] __attribute__((aligned(16)));

/* 0x005CF370 */
MenuItem *trade_stock_list_page_mark[4] __attribute__((aligned(16)));

/* 0x005CF380 */
MenuItem *trade_stock_list_cursor[4] __attribute__((aligned(16)));

/* 0x005CF390 */
MenuItem *smith_collect_list_top[4] __attribute__((aligned(16)));

/* 0x005CF3A0 */
MenuItem *smith_collect_list_page_mark[4] __attribute__((aligned(16)));

/* 0x005CF3B0 */
MenuItem *smith_collect_list_cursor[4] __attribute__((aligned(16)));

/* 0x005CF3C0 */
SaveScreen save_screen_state __attribute__((aligned(16)));
