/* rodata_0043E570.c: the game's data from the decomp's rodata_0043E570.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/game.h"
#include "model/model.h"

/* 0x0043E570 */
char map_file_name_format[16] = "%3.3d.map";

/* 0x0043E710 */
char map_load_file_name_format[16] = "%3.3d.MAP";

/* 0x0043E720 */
char map_change_file_name_format[16] = "%3.3d.MAP";

/* 0x0043E730 */
char viewer_texture_file_name_format[24] = "map_view/%3.3d.tx2";

/* 0x0043E748 */
char viewer_pointer_model_path[24] = "map_view/pointer.omd";

/* 0x0043E760 */
ViewerCornerTable viewer_marker_corner_us __attribute__((aligned(16))) = { .values = { 0, 1024, 0, 1024 } };

/* 0x0043E770 */
ViewerCornerTable viewer_marker_corner_vs __attribute__((aligned(16))) = { .values = { 0, 0, 1024, 1024 } };

/* 0x0043E780 */
Qword object_item_scale __attribute__((aligned(16))) = { .unk00 = 0x3F8000003F800000ULL, .unk08 = 0x3F800000ULL };

/* 0x0043E790 */
char chr_file_path_format[13] = "%sc%4.4d.chr";
