/* Included first in each of the decomp's libm files the port compiles (decomp/src/sce/libm, see
   CMakeLists.txt): their external names become ps2_*, so they never meet the host's libm. The double
   functions are ps2_soft_*: port/sdk/libm_ps2.c wraps them (port/include/port/libm_ps2.h). */
#ifndef PORT_LIBM_PS2_NAMES_H
#define PORT_LIBM_PS2_NAMES_H

/* float */
#define sinf ps2_sinf
#define cosf ps2_cosf
#define atan2f ps2_atan2f
#ifndef atanf /* (sf_atan.c's is ps2_atanf_newlib: port/sdk/libm_ps2.c wraps it) */
#define atanf ps2_atanf
#endif
#define asinf ps2_asinf
#define acosf ps2_acosf
#define sqrtf ps2_sqrtf
#define fabsf ps2_fabsf
#define floorf ps2_floorf
#define fmodf ps2_fmodf
#define copysignf ps2_copysignf
#define scalbnf ps2_scalbnf
#define isnanf ps2_isnanf
#define __ieee754_acosf ps2___ieee754_acosf
#define __ieee754_asinf ps2___ieee754_asinf
#define __ieee754_atan2f ps2___ieee754_atan2f
#define __ieee754_fmodf ps2___ieee754_fmodf
#define __ieee754_rem_pio2f ps2___ieee754_rem_pio2f
#define __ieee754_sqrtf ps2___ieee754_sqrtf
#define __kernel_cosf ps2___kernel_cosf
#define __kernel_sinf ps2___kernel_sinf
#define __kernel_rem_pio2f ps2___kernel_rem_pio2f

/* double (soft-float on the EE: libgcc's dp-bit, IEEE rounding to nearest) */
#define sin ps2_soft_sin
#define cos ps2_soft_cos
#define sqrt ps2_soft_sqrt
#define atan2 ps2_soft_atan2
#define atan ps2_soft_atan
#define acos ps2_soft_acos
#define fabs ps2_fabs
#define floor ps2_floor
#define scalbn ps2_scalbn
#define copysign ps2_copysign
#define __ieee754_acos ps2___ieee754_acos
#define __ieee754_atan2 ps2___ieee754_atan2
#define __ieee754_rem_pio2 ps2___ieee754_rem_pio2
#define __ieee754_sqrt ps2___ieee754_sqrt
#define __kernel_cos ps2___kernel_cos
#define __kernel_sin ps2___kernel_sin
#define __kernel_rem_pio2 ps2___kernel_rem_pio2

/* newlib's support */
#define matherr ps2_matherr
#define isnan ps2_isnan
#define isinf ps2_isinf
#define __fdlib_version ps2___fdlib_version
#define __errno ps2___errno
#define errno ps2_errno

#endif
