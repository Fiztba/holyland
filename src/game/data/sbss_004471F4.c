/* sbss_004471F4.c: the game's data from the decomp's sbss_004471F4.sbss.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/load.h"
#include "gfx/packet.h"
#include "ui/menu.h"

/* 0x00447200 */
s32 menu_even_field;

/* 0x00447204 */
s32 pad_mode_change_frames;

/* 0x00447208 */
u8 D_00447208;

/* 0x00447210 */
MenuItem *visible_bar_top;

/* 0x00447214 */
MenuItem *visible_bar_cursor;

/* 0x00447218 */
MenuItem *visible_names_top;

/* 0x0044721C */
MenuItem *visible_names_cursor;

/* 0x00447220 */
MenuItem *visible_text_item;

/* 0x00447224 */
u8 card_save_mask;

/* 0x00447228 */
f32 brightness_slider;

/* 0x0044722C */
SpritePacket *main_parts;

/* 0x00447230 */
SpritePacket *item_parts;

/* 0x00447234 */
SpritePacket *magic_parts;

/* 0x00447238 */
SpritePacket *equip_parts;

/* 0x0044723C */
SpritePacket *equip_a_parts;

/* 0x00447240 */
SpritePacket *equip_g_parts;

/* 0x00447244 */
SpritePacket *equip_m_parts;

/* 0x00447248 */
SpritePacket *item_sel_parts;

/* 0x0044724C */
SpritePacket *status_parts;

/* 0x00447250 */
SpritePacket *image_parts;

/* 0x00447254 */
SpritePacket *recol_parts;

/* 0x00447258 */
SpritePacket *system_parts;

/* 0x0044725C */
SpritePacket *option_parts;

/* 0x00447260 */
SpritePacket *keylist_parts;

/* 0x00447264 */
SpritePacket *keyconf_parts;

/* 0x00447268 */
SpritePacket *equip_g2_parts;

/* 0x0044726C */
SpritePacket *equip_s_parts;

/* 0x00447270 */
SpritePacket *recol_a_parts;

/* 0x00447274 */
SpritePacket *recol_g_parts;

/* 0x00447278 */
SpritePacket *recol_g2_parts;

/* 0x004472B0 */
ChunkPoolNode *free_load_buffers;

/* 0x004472B8 */
MenuFreeNode D_004472B8;

/* 0x004472C4 */
s8 main_menu_state;

/* 0x004472C5 */
s8 use_item_state;

/* 0x004472C6 */
s8 equip_state;

/* 0x004472C7: the second byte of splat's label equip_state */
s8 equip_panel_state;

/* 0x004472C8 */
s8 cast_spell_state;

/* 0x004472C9 */
s8 key_items_state;

/* 0x004472CA */
s8 recollection_state;

/* 0x004472CB */
s8 visible_item_state;

/* 0x004472CC */
s8 visible_panel_state;

/* 0x004472CD */
s8 main_menu_line;

/* 0x004472CE */
char equip_model_shown;

/* 0x004472CF */
u8 brightness_before;

/* 0x004472D0 */
int pad_mode;

/* 0x004472D4 */
int pad_mode_before;

/* 0x004472D8 */
s8 recollection_cursor;

/* 0x004472D9 */
s8 recollection_top;

/* 0x004472DA */
s8 menu_input_enabled;

/* 0x004472E0 */
s32 equip_group_shown;

/* 0x004472E8 */
s32 system_menu_line;

/* 0x004472F0 */
int worn_model_loading;

/* 0x00447304 */
s32 trade_pad_change_frames;

/* 0x00447308 */
s32 smith_pad_change_frames;

/* 0x0044730C */
int save_screen_pad_change_frames;

/* 0x00447310 */
s8 D_00447310;

/* 0x00447311 */
s8 trade_screen_state;

/* 0x00447312 */
s8 trade_panel_state;

/* 0x00447313 */
s8 smith_confirm_shown;

/* 0x00447314 */
s8 D_00447314;

/* 0x00447315 */
s8 smith_upgrade_choice;

/* 0x00447316 */
s8 smith_screen_state;

/* 0x00447317 */
s8 smith_panel_state;

/* 0x00447318 */
s8 modal_input_locked;

/* 0x0044731C */
s32 modal_pad_mode;

/* 0x00447320 */
s32 modal_pad_mode_last;

/* 0x00447324 */
s32 save_lines_reorder_pending;

/* 0x00447328 */
s32 modal_even_field;
