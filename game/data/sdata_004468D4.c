/* sdata_004468D4.c: the game's data from the decomp's sdata_004468D4.sdata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "collision/collision.h"
#include "core/game.h"
#include "entity/entity.h"
#include "gfx/gs.h"
#include "map/map.h"
#include "model/model.h"
#include "player/player.h"
#include "ui/menu.h"

extern ModelInstance held_item_model; /* player/held_weapon.c */

/* 0x004468DC */
int floor_skim_spawn_count;

/* 0x004468E0 */
int ground_fan_index;

/* 0x004468E4 */
int bolt_start_count;

/* 0x004468E8 */
int turn_fan_index;

/* 0x004468F0 */
char debug_cursor_mark[8] = ">>";

/* 0x004468F8 */
char debug_cursor_blank[8] = "  ";

/* 0x00446900 */
char debug_ini_prompt[8] = "INI:";

/* 0x00446908 */
char debug_ini_digit_format[8] = "%d";

/* 0x00446910 */
char debug_ini_line_end[8] = " \n";

/* 0x00446918 */
CollisionListPick2 map_hit_floor_strip_lists[1] = { { .lists = { 1, 5 } } };

/* 0x00446920 */
CollisionListPick2 map_hit_wall_strip_lists[1] = { { .lists = { 0, 2 } } };

/* 0x00446928 */
s32 dark_one_hits_left;

/* 0x0044692C */
u16 forward_button;

/* 0x0044692E */
u16 back_button;

/* 0x00446930 */
u16 turn_right_button;

/* 0x00446932 */
u16 turn_left_button;

/* 0x00446934 */
u16 step_right_button;

/* 0x00446936 */
u16 step_left_button;

/* 0x00446938 */
u16 look_down_button;

/* 0x0044693A */
u16 look_up_button;

/* 0x0044693C */
u16 attack_button;

/* 0x0044693E */
u16 magic_button;

/* 0x00446940 */
u16 action_button;

/* 0x00446942 */
u16 menu_button;

/* 0x00446944 */
u16 shortcut_button;

/* 0x00446946 */
u16 pause_button;

/* 0x00446949 */
u8 rumble_active;

/* 0x0044694A */
u8 rumble_strength;

/* 0x0044694B */
u8 rumble_frames_left;

/* 0x0044694C */
u8 rumble_frames;

/* 0x0044694D */
u8 rumble_fades;

/* 0x0044694E */
u8 rumble_paused;

/* 0x00446950 */
int sacrificial_mask_cooldown;

/* 0x00446954 */
f32 D_00446954[1] = { 3.4028235e+38f };

/* 0x00446958 */
s8 footstep_voice;

/* 0x0044695C */
int recent_damage_frames;

/* 0x00446960 */
s32 timed_boost_order;

/* 0x00446964 */
PlayerShot *channeled_shot;

/* 0x00446968 */
f32 D_00446968[1] = { 3.4028235e+38f };

/* 0x0044696C */
f32 D_0044696C[1] = { 3.4028235e+38f };

/* 0x00446970 */
int loaded_arm_gauntlet = -1;

/* 0x00446974 */
int loaded_held_weapon = -1;

/* 0x00446978 */
EntityModel *held_item_model_ptr = (EntityModel *)&held_item_model;

/* 0x00446980 */
f32 D_00446980[1] = { -0.0f };

/* 0x00446984 */
const f32 D_00446984[1] = { -0.0f };

/* 0x00446988 */
const f32 D_00446988[1] = { 3.4028235e+38f };

/* 0x0044698C */
const f32 D_0044698C[1] = { 3.4028235e+38f };

/* 0x00446990 */
const f32 D_00446990[1] = { 3.4028235e+38f };

/* 0x00446994 */
const f32 D_00446994[1] = { 3.4028235e+38f };

/* 0x00446998 */
s32 segment_strip_lists[2] = { 0, 2 };

/* 0x004469A0 */
const f32 D_004469A0[1] = { 3.4028235e+38f };

/* 0x004469A4 */
f32 D_004469A4[1] = { 3.4028235e+38f };

/* 0x004469A8 */
f32 D_004469A8[1] = { 3.4028235e+38f };

/* 0x004469AC */
f32 D_004469AC[1] = { 3.4028235e+38f };

/* 0x004469B0 */
f32 D_004469B0[1] = { 3.4028235e+38f };

/* 0x004469B4 */
f32 D_004469B4[1] = { 3.4028235e+38f };

/* 0x004469B8 */
MapItemDef *map_item_defs[2];

/* 0x004469C0 */
s16 *map_item_model_kinds[2];

/* 0x004469C8 */
u8 *map_item_group_thresholds[2];

/* 0x004469D0 */
s32 item_drop_counter;

/* 0x004469D4 */
ItemNode *lying_items;

/* 0x004469D8 */
ItemNode *pickup_item;

/* 0x004469DC */
f32 D_004469DC[1] = { 3.4028235e+38f };

/* 0x004469E0 */
MapActor *map_actors_in_use;

/* 0x004469E8 */
int voice_load_step;

/* 0x004469F0 */
char sample_buffer_freed_message[8] = "@#$%";

/* 0x004469F8 */
int sequence_load_step;

/* 0x00446A08 */
f32 D_00446A08[1] = { 3.4028235e+38f };

/* 0x00446A0C */
float D_00446A0C[1] = { 3.4028235e+38f };

/* 0x00446A10 */
ClockTime play_clock_start;

/* 0x00446A18 */
ClockTime play_time_base;

/* 0x00446A20 */
ClockTime wall_clock_time;

/* 0x00446A28 */
ClockTime current_play_time;

/* 0x00446A30 */
u32 main_part_count;

/* 0x00446A34 */
u32 item_part_count;

/* 0x00446A38 */
u32 magic_part_count;

/* 0x00446A3C */
u32 equip_part_count;

/* 0x00446A40 */
u32 equip_a_part_count;

/* 0x00446A44 */
u32 equip_g_part_count;

/* 0x00446A48 */
u32 equip_m_part_count;

/* 0x00446A4C */
u32 item_sel_part_count;

/* 0x00446A50 */
u32 status_part_count;

/* 0x00446A54 */
u32 image_part_count;

/* 0x00446A58 */
u32 recol_part_count;

/* 0x00446A5C */
u32 system_part_count;

/* 0x00446A60 */
u32 slot_part_count;

/* 0x00446A64 */
u32 data_part_count;

/* 0x00446A68 */
u32 option_part_count;

/* 0x00446A6C */
u32 keylist_part_count;

/* 0x00446A70 */
u32 keyconf_part_count;

/* 0x00446A74 */
u32 preshop_part_count;

/* 0x00446A78 */
u32 shop_part_count;

/* 0x00446A7C */
u32 equip_g2_part_count;

/* 0x00446A80 */
u32 equip_s_part_count;

/* 0x00446A84 */
u32 recol_a_part_count;

/* 0x00446A88 */
u32 recol_g_part_count;

/* 0x00446A8C */
u32 recol_g2_part_count;

/* 0x00446A94 */
u32 shop_g2_part_count;

/* 0x00446A98 */
u32 shop_a_part_count;

/* 0x00446A9C */
u32 shop_g_part_count;

/* 0x00446AA0 */
u32 pretune_part_count;

/* 0x00446AA4 */
u32 tune_part_count;

/* 0x00446AA8 */
u32 tune_s_part_count;

/* 0x00446AAC */
u32 mc_part_count;

/* 0x00446AB0 */
u32 bright_part_count;

/* 0x00446AB4 */
u32 shop_mess_part_count;

/* 0x00446AB8 */
u32 tune_mess_part_count;

/* 0x00446ABC */
u32 mc_mess_part_count;

/* 0x00446AC0 */
u32 equip_m2_part_count;

/* 0x00446AC4 */
s32 menu_run_step;

/* 0x00446AC8 */
s32 menu_step;

/* 0x00446ACC */
s32 D_00446ACC;

/* 0x00446AD0 */
int model_read_file = -1;

/* 0x00446AD4 detail_panel_open (0x446AD4) and equipped_list_open (0x446AD5), one word for ui/trade.c */
DetailPanelFlags detail_panel_flags;

/* 0x00446AE0 */
char sunday_name[8] = "\xC6\xFC\xCD\xCB\xC6\xFC";

/* 0x00446AE8 */
char monday_name[8] = "\xB7\xEE\xCD\xCB\xC6\xFC";

/* 0x00446AF0 */
char tuesday_name[8] = "\xB2\xD0\xCD\xCB\xC6\xFC";

/* 0x00446AF8 */
char wednesday_name[8] = "\xBF\xE5\xCD\xCB\xC6\xFC";

/* 0x00446B00 */
char thursday_name[8] = "\xCC\xDA\xCD\xCB\xC6\xFC";

/* 0x00446B08 */
char friday_name[8] = "\xB6\xE2\xCD\xCB\xC6\xFC";

/* 0x00446B10 */
char saturday_name[8] = "\xC5\xDA\xCD\xCB\xC6\xFC";

/* 0x00446B18 */
ColorBytes backdrop_shade_color[1] = { { .red = 16, .green = 16, .blue = 16 } };

/* 0x00446B20 */
void *menu_screen_handler;

/* 0x00446B24 */
void *save_list_opener;

/* 0x00446B28 */
char equip_screen_name[8] = "Equip";

/* 0x00446B39 */
u8 D_00446B39;

/* 0x00446B3C */
f32 use_item_model_angle;

/* 0x00446B40 */
s32 use_item_frames;

/* 0x00446B50 */
s8 detail_panel_sound_pending;

/* 0x00446B54 */
f32 equip_item_model_angle;

/* 0x00446B58 */
f32 equip_weapon_model_angle;

/* 0x00446B5C */
u32 equip_weapon_motion_frame;

/* 0x00446B60 */
char group_name_weapons[8] = "weapons";

/* 0x00446B68 */
char group_name_magic[8] = "magic";

/* 0x00446B70 */
char group_name_shield[8] = "shield";

/* 0x00446B78 */
char group_name_helm[8] = "helm";

/* 0x00446B80 */
char group_name_armor[8] = "armor";

/* 0x00446B88 */
char group_name_gauntlet[8] = "guntlet";

/* 0x00446B90 */
char group_name_boot[8] = "boot";

/* 0x00446B98 */
char group_name_ring[8] = "ring";

/* 0x00446BA0 */
char group_name_bracelet[8] = "bras";

/* 0x00446BA8 */
char group_name_amulet[8] = "amu";

/* 0x00446BB0 */
char group_name_item[8] = "item";

/* 0x00446BB8 */
s32 key_item_row;

/* 0x00446BBC */
s32 key_items_frames;

/* 0x00446BC0 */
s32 recollection_page;

/* 0x00446BC4 */
s32 recollection_page_before;

/* 0x00446BD0 */
f32 visible_item_model_angle;
