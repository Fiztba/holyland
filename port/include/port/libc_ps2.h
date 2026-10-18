/* The PS2 C library functions the game calls whose host versions differ (port/sdk/libc_ps2.c).
   Include it after the system headers: it turns the game's mktime calls into newlib's. */
#ifndef PORT_LIBC_PS2_H
#define PORT_LIBC_PS2_H

int ps2_mktime(int *fields);
#define mktime(fields) ps2_mktime((int *)(fields))

#endif
