/* port/platform/int_divide.c: INT_MIN / -1 gives what the EE's DIV leaves (LO = INT_MIN, HI = 0)
   instead of x86's trap, for 32 and 64 bits and the operand forms the compilers emit (a register, a
   local in memory, a global RIP-relative). A division by zero is not caught: run with "zero" it
   should end the program after saying where.

     int_divide_test [zero] */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "port/platform.h"

static int failed, checked;

static void check(const char *what, int64_t got, int64_t want) {
    checked++;
    if (got != want) {
        failed++;
        printf("FAIL %s: %lld, not %lld\n", what, (long long)got, (long long)want);
    } else {
        printf("ok   %s\n", what);
    }
}

volatile int32_t g_min = INT32_MIN, g_minus1 = -1;
volatile int64_t g_min64 = INT64_MIN, g_minus1_64 = -1;
int32_t g_divisor = -1;

__attribute__((noinline)) static int32_t div_reg(int32_t a, int32_t b) {
    return a / b;
}

__attribute__((noinline)) static int32_t rem_reg(int32_t a, int32_t b) {
    return a % b;
}

__attribute__((noinline)) static int32_t div_global(int32_t a) {
    return a / g_divisor; /* (a RIP-relative operand, if the compiler keeps it in memory) */
}

__attribute__((noinline)) static int64_t div64(int64_t a, int64_t b) {
    return a / b;
}

__attribute__((noinline)) static int64_t rem64(int64_t a, int64_t b) {
    return a % b;
}

__attribute__((noinline)) static int32_t div_mem(volatile int32_t *a, volatile int32_t *b) {
    int32_t x = *a;

    __asm__ volatile("" ::: "memory");
    return x / *b;
}

int main(int argc, char **argv) {
    port_int_divide_init();
    if (argc > 1 && strcmp(argv[1], "zero") == 0) {
        volatile int32_t zero = 0;

        printf("dividing by zero\n");
        printf("%d\n", div_reg(g_min, zero));
        printf("FAIL: still running\n");
        return 1;
    }
    check("INT_MIN / -1 (register)", div_reg(g_min, g_minus1), INT32_MIN);
    check("INT_MIN % -1 (register)", rem_reg(g_min, g_minus1), 0);
    check("INT_MIN / -1 (global)", div_global(g_min), INT32_MIN);
    check("INT_MIN / -1 (memory)", div_mem(&g_min, &g_minus1), INT32_MIN);
    check("INT64_MIN / -1", div64(g_min64, g_minus1_64), INT64_MIN);
    check("INT64_MIN % -1", rem64(g_min64, g_minus1_64), 0);
    check("7 / -1 (no trap)", div_reg(7, g_minus1), -7);
    printf("%d of %d checks failed\n", failed, checked);
    return failed != 0;
}
