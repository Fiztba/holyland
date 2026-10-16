/* rodata_004412DC.c: the game's data from the decomp's rodata_004412DC.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "model/model.h"

/* 0x004412E0 */
char opening_movie_path[32] = "cdrom0:\\DATA\\KF4OP.PSS;1";

/* 0x00441300 */
char ending_movie_path[32] = "cdrom0:\\DATA\\KF4ED.PSS;1";

/* 0x00441320 */
char staff_roll_movie_path[48] = "cdrom0:\\DATA\\KF4STAFF.PSS;1";

/* 0x00441350 */
char ioprp_image_path[24] = "cdrom0:\\IOPRP224.IMG;1";

/* 0x00441368 */
char sio2man_module_path[32] = "cdrom0:\\IOP\\SIO2MAN.IRX;1";

/* 0x00441388 */
char padman_module_path[32] = "cdrom0:\\IOP\\PADMAN.IRX;1";

/* 0x004413A8 */
char mcman_module_path[24] = "cdrom0:\\IOP\\MCMAN.IRX;1";

/* 0x004413C0 */
char mcserv_module_path[32] = "cdrom0:\\IOP\\MCSERV.IRX;1";

/* 0x004413E0 */
char libsd_module_path[24] = "cdrom0:\\IOP\\LIBSD.IRX;1";

/* 0x004413F8 */
char modmidi_module_path[32] = "cdrom0:\\IOP\\MODMIDI.IRX;1";

/* 0x00441418 */
char modhsyn_module_path[32] = "cdrom0:\\IOP\\MODHSYN.IRX;1";

/* 0x00441438 */
char sdrdrv_module_path[32] = "cdrom0:\\IOP\\SDRDRV.IRX;1";

/* 0x00441458 */
char fsiopsnd_module_path[32] = "cdrom0:\\IOP\\FSIOPSND.IRX;1";

/* 0x00441478 */
char vector4_print_format[24] = "(%f,%f,%f,%f)\n";

/* 0x00441490 */
Vector4Arg mirrored_flat_offset __attribute__((aligned(16))) = { .y = -160000.0f, .z = 32768.0f, .w = 1.0f };

/* 0x004414A0 */
char tm2_tbp_zero_message[24] = "load_tm2() tbp:0 !!!!";

/* 0x004414B8 */
char tm2_header_label[16] = "load_tm2()";

/* 0x004414C8 */
char tex0_tbp_format[16] = "TBP[%u] ";

/* 0x004414D8 */
char tex0_tbw_format[16] = "TBW[%u] ";

/* 0x004414E8 */
char tex0_psm_format[16] = "PSM[0x%x] ";

/* 0x004414F8 */
char tex0_tcc_format[16] = "TCC[%u] ";

/* 0x00441508 */
char tex0_tfx_format[16] = "TFX[0x%x] ";

/* 0x00441518 */
char tex0_cbp_format[16] = "CBP[%u] ";

/* 0x00441528 */
char tex0_cpsm_format[16] = "CPSM[0x%x] ";

/* 0x00441538 */
char tex0_csm_format[16] = "CSM[%u] ";

/* 0x00441548 */
char tex0_csa_format[16] = "CSA[%u] ";

/* 0x00441558 */
char tex0_cld_format[11] = "CLD[0x%x]\n";
