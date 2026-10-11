#define NULL ((void *)0)
typedef unsigned int bitwise_t;
typedef int s32;
typedef unsigned int u32;
typedef unsigned char u8;
typedef signed char s8;
typedef short s16;
typedef unsigned short u16;
typedef float f32;
typedef double f64;
void func_00195898(char *arg0, u8 arg1) {
    u8 temp_2;

    temp_2 = arg0[0xCC];
    arg0[0xCC] = arg1;
    arg0[0xCE] = temp_2;
}
