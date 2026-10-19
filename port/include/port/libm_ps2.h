/* The PS2's math library (port/sdk/libm_ps2.c), which the build includes first in every game file
   (CMakeLists.txt): the game's calls to newlib's functions go to the PS2's versions, not the host's,
   whose results differ in the last bits (and between Windows and Linux). The macros take the place of
   the names only where they are called (or declared, as the decomp's files do), and keep the compiler
   from treating them as its builtins (folding them, or sqrtf as an SSE square root).

   ps2_dtof is a double turned into a float as the EE's software doubles do it (dp-bit's dptofp,
   rounding to nearest): the game's threads round toward zero (port/platform/fiber.c), so a C
   conversion would truncate where the PS2 rounds. Each place the game turns a double into a float
   says ps2_dtof (docs/port/RULES.md, "PS2 numbers"). */
#ifndef PORT_LIBM_PS2_H
#define PORT_LIBM_PS2_H

/* float: the EE's FPU (as the game's own float code) */
float ps2_sinf(float x);
float ps2_cosf(float x);
float ps2_atan2f(float y, float x);
float ps2_atanf(float x);
float ps2_asinf(float x);
float ps2_sqrtf(float x);
/* double: software, IEEE rounding to nearest */
double ps2_sin(double x);
double ps2_cos(double x);
double ps2_sqrt(double x);
double ps2_atan2(double y, double x);
double ps2_atan(double x);
double ps2_acos(double x);
float ps2_dtof(double x);

#define sinf(x) ps2_sinf(x)
#define cosf(x) ps2_cosf(x)
#define atan2f(y, x) ps2_atan2f(y, x)
#define atanf(x) ps2_atanf(x)
#define asinf(x) ps2_asinf(x)
#define sqrtf(x) ps2_sqrtf(x)
#define sin(x) ps2_sin(x)
#define cos(x) ps2_cos(x)
#define sqrt(x) ps2_sqrt(x)
#define atan2(y, x) ps2_atan2(y, x)
#define atan(x) ps2_atan(x)
#define acos(x) ps2_acos(x)

#endif
