typedef int s32;
typedef unsigned int u32;
typedef unsigned char u8;
typedef signed char s8;
typedef short s16;
typedef unsigned short u16;
typedef float f32;
typedef double f64;
#define NULL ((void *)0)
#define NULL ((void *)0)
s32 func_0021DFD8(char *arg0, s32 arg1) {
    s32 temp_2;

    temp_2 = (*(int *)((char *)arg0 + 0xA8));
    (*(int *)((char *)arg0 + 0xA8)) = arg1;
    return temp_2;
}
