typedef int s32;
typedef unsigned int u32;
typedef unsigned char u8;
typedef signed char s8;
typedef short s16;
typedef unsigned short u16;
typedef float f32;
typedef double f64;
s32 func_00194EE8(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
    s32 temp_2;
    s32 temp_2_2;

    temp_2_2 = arg0 + arg1;
    temp_2 = (temp_2_2 < arg2) ? arg2 : temp_2_2;
    return (arg3 < temp_2) ? arg3 : temp_2;
}
