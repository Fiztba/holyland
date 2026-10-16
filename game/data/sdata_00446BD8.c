/* sdata_00446BD8.c: the game's data from the decomp's sdata_00446BD8.sdata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/state.h"
#include "gfx/gs.h"
#include "gfx/packet.h"
#include "ui/link.h"
#include "ui/menu.h"

/* 0x00446BD8 */
s32 quit_choice = 1;

/* 0x00446BDC */
s8 card_slot_leave_steps;

/* 0x00446BDD */
s8 save_list_leave_steps;

/* 0x00446BE0 */
s32 sound_mode_before;

/* 0x00446BE4 */
s8 sound_mode_change;

/* 0x00446BE5 */
s8 options_leave_steps;

/* 0x00446BE6 */
s8 buttons_leave_steps;

/* 0x00446BE8 */
s32 button_line;

/* 0x00446BEC */
s32 button_prompt_choice;

/* 0x00446BF0 */
s8 button_prompt;

/* 0x00446BF4 */
SpritePacket *slot_parts;

/* 0x00446BF8 */
SpritePacket *data_parts;

/* 0x00446BFC */
SpritePacket *preshop_parts;

/* 0x00446C00 */
SpritePacket *shop_parts;

/* 0x00446C04 */
f32 pulse_sprite_alpha;

/* 0x00446C08 */
s8 pulse_sprite_falling;

/* 0x00446C09 */
s8 pulse_value_falling;

/* 0x00446C0C */
SpritePacket *shop_g2_parts;

/* 0x00446C10 */
SpritePacket *shop_a_parts;

/* 0x00446C14 */
SpritePacket *shop_g_parts;

/* 0x00446C1C */
MenuItem *handover_list_first;

/* 0x00446C20 */
MenuItem *handover_list_last_page;

/* 0x00446C24 */
MenuItem *owned_list_first;

/* 0x00446C28 */
MenuItem *owned_list_last_page;

/* 0x00446C34 */
s32 handover_list_count;

/* 0x00446C38 */
s32 owned_list_count;

/* 0x00446C40 */
MenuItem *item_list_first;

/* 0x00446C44 */
MenuItem *item_list_last_page;

/* 0x00446C48 */
s32 item_list_count;

/* 0x00446C4C */
SpritePacket *pretune_parts;

/* 0x00446C50 */
SpritePacket *tune_parts;

/* 0x00446C54 */
SpritePacket *tune_s_parts;

/* 0x00446C58 */
SpritePacket *mc_parts;

/* 0x00446C5C */
SpritePacket *bright_parts;

/* 0x00446C60 */
SpritePacket *shop_mess_parts;

/* 0x00446C64 */
SpritePacket *tune_mess_parts;

/* 0x00446C68 */
SpritePacket *mc_mess_parts;

/* 0x00446C6C */
SpritePacket *equip_m2_parts;

/* 0x00446C70 */
s8 system_screen_state;

/* 0x00446C78. The decomp's labels menu_draw_color_alpha lie inside it (menu_draw_color.alpha): the code that names them must use these
   fields, so they are not defined on their own */
ColorBytes menu_draw_color;

/* 0x00446C80 */
ColorBytes panel_text_color;

/* 0x00446C88. The decomp's labels model_draw_color_alpha lie inside it (model_draw_color.alpha): the code that names them must use these
   fields, so they are not defined on their own */
ColorBytes model_draw_color;

/* 0x00446C90 */
LinkNode *model_load_chain;

/* 0x00446C94 */
s8 menu_mode;

/* 0x00446C98 */
s32 menu_frame_ticks;

/* 0x00446C9C */
u8 trade_panel_shown;

/* 0x00446CA0 */
void (*trade_handler)(HandlerContext *);

/* 0x00446CA4 */
int trade_step;

/* 0x00446CA8 */
s32 trade_run_step;

/* 0x00446CAC */
s32 trade_stock_list_panel_sound_pending;

/* 0x00446CB0 */
f32 trade_stock_list_model_angle;

/* 0x00446CB4 */
s32 trade_take_amount_panel_sound_pending;

/* 0x00446CB8 */
f32 trade_take_amount_model_angle;

/* 0x00446CBC */
f32 trade_owned_list_model_angle;

/* 0x00446CC0 */
f32 trade_give_amount_model_angle;

/* 0x00446CC4 */
void (*smith_handler)(HandlerContext *);

/* 0x00446CC8 */
s32 smith_step;

/* 0x00446CCC */
s32 smith_run_step;

/* 0x00446CD0 */
s8 smith_choices_active = 1;

/* 0x00446CD4 */
s32 smith_upgrade_list_panel_sound_pending;

/* 0x00446CD8 */
f32 smith_upgrade_list_model_angle;

/* 0x00446CDC */
s32 smith_upgrade_confirm_panel_sound_pending;

/* 0x00446CE0 */
f32 smith_upgrade_confirm_model_angle;

/* 0x00446CE4 */
s32 smith_confirm_list_panel_sound_pending;

/* 0x00446CE8 */
f32 smith_confirm_list_model_angle;

/* 0x00446CEC */
f32 smith_item_screen_model_angle;

/* 0x00446CF0 */
s32 smith_collect_list_panel_sound_pending;

/* 0x00446CF4 */
f32 smith_collect_list_model_angle;

/* 0x00446CF8 */
int (*save_screen_handler)();

/* 0x00446CFC */
int save_screen_step;

/* 0x00446D00 */
s32 save_screen_run_step;
