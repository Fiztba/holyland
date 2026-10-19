/* The PS2's math library (port/sdk/libm_ps2.c) against the retail program's own: each line of
   tests/libm_ref.txt is an input and the result tools/ee_libm_ref.py got running SLUS_203.18's
   function on a model of the EE. Run with the game threads' float mode (MXCSR rounding toward zero,
   FTZ, DAZ), as the game calls them.

     libm_test [tests/libm_ref.txt]

   Also checks ps2_dtof (dp-bit's double -> float) on hand-picked cases. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/libm_ps2.h"
#include "port/platform.h"

static int failed, checked;

static uint32_t fbits(float f) {
    uint32_t b;

    memcpy(&b, &f, 4);
    return b;
}

static float bitsf(uint32_t b) {
    float f;

    memcpy(&f, &b, 4);
    return f;
}

static uint64_t dbits(double d) {
    uint64_t b;

    memcpy(&b, &d, 8);
    return b;
}

static double bitsd(uint64_t b) {
    double d;

    memcpy(&d, &b, 8);
    return d;
}

static void dtof(uint64_t in, uint32_t want, const char *what) {
    uint32_t got = fbits(ps2_dtof(bitsd(in)));

    checked++;
    if (got != want) {
        failed++;
        printf("FAIL ps2_dtof %s: %016llx gives %08x, not %08x\n", what, (unsigned long long)in, got, want);
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "tests/libm_ref.txt";
    FILE *f = fopen(path, "r");
    char line[256];
    int per_name_bad[16] = {0};
    static const char *names[] = {"sinf", "cosf", "atanf", "asinf", "sqrtf", "atan2f", "sin", "cos",
                                  "sqrt", "atan", "acos", "atan2"};
    int n;

    if (f == NULL) {
        fprintf(stderr, "libm_test: cannot read %s\n", path);
        return 2;
    }
#if defined(__x86_64__) || defined(__i386__)
    _mm_setcsr(PORT_MXCSR_EE);
#endif
    while (fgets(line, sizeof(line), f)) {
        char name[16];
        unsigned long long a, b, c;
        int fields;
        int which = -1;
        int bad = 0;

        if (line[0] == '#') {
            continue;
        }
        fields = sscanf(line, "%15s %llx %llx %llx", name, &a, &b, &c);
        for (n = 0; n < 12; n++) {
            if (strcmp(name, names[n]) == 0) {
                which = n;
            }
        }
        if (which < 0 || fields < 3) {
            fprintf(stderr, "libm_test: bad line: %s", line);
            return 2;
        }
        checked++;
        switch (which) {
        case 0: bad = fbits(ps2_sinf(bitsf((uint32_t)a))) != b; break;
        case 1: bad = fbits(ps2_cosf(bitsf((uint32_t)a))) != b; break;
        case 2: bad = fbits(ps2_atanf(bitsf((uint32_t)a))) != b; break;
        case 3: bad = fbits(ps2_asinf(bitsf((uint32_t)a))) != b; break;
        case 4: bad = fbits(ps2_sqrtf(bitsf((uint32_t)a))) != b; break;
        case 5: bad = fbits(ps2_atan2f(bitsf((uint32_t)a), bitsf((uint32_t)b))) != c; break;
        case 6: bad = dbits(ps2_sin(bitsd(a))) != b; break;
        case 7: bad = dbits(ps2_cos(bitsd(a))) != b; break;
        case 8: bad = dbits(ps2_sqrt(bitsd(a))) != b; break;
        case 9: bad = dbits(ps2_atan(bitsd(a))) != b; break;
        case 10: bad = dbits(ps2_acos(bitsd(a))) != b; break;
        case 11: bad = dbits(ps2_atan2(bitsd(a), bitsd(b))) != c; break;
        }
        if (bad) {
            failed++;
            if (per_name_bad[which]++ < 3) {
                unsigned long long got = 0;

                switch (which) {
                case 0: got = fbits(ps2_sinf(bitsf((uint32_t)a))); break;
                case 1: got = fbits(ps2_cosf(bitsf((uint32_t)a))); break;
                case 2: got = fbits(ps2_atanf(bitsf((uint32_t)a))); break;
                case 3: got = fbits(ps2_asinf(bitsf((uint32_t)a))); break;
                case 4: got = fbits(ps2_sqrtf(bitsf((uint32_t)a))); break;
                case 5: got = fbits(ps2_atan2f(bitsf((uint32_t)a), bitsf((uint32_t)b))); break;
                case 6: got = dbits(ps2_sin(bitsd(a))); break;
                case 7: got = dbits(ps2_cos(bitsd(a))); break;
                case 8: got = dbits(ps2_sqrt(bitsd(a))); break;
                case 9: got = dbits(ps2_atan(bitsd(a))); break;
                case 10: got = dbits(ps2_acos(bitsd(a))); break;
                case 11: got = dbits(ps2_atan2(bitsd(a), bitsd(b))); break;
                }
                printf("FAIL %s: got %llx for: %s", name, got, line);
            }
        }
    }
    fclose(f);
    for (n = 0; n < 12; n++) {
        if (per_name_bad[n]) {
            printf("     %s: %d differ\n", names[n], per_name_bad[n]);
        }
    }

    /* dp-bit's conversion: to nearest even, with the sticky bits; truncated in the denormal range */
    dtof(0x3FF0000000000000ull, 0x3F800000u, "1.0");
    dtof(0x3FF0000010000000ull, 0x3F800000u, "1 + 2^-24 (a tie) to even");
    dtof(0x3FF0000030000000ull, 0x3F800002u, "1 + 3 * 2^-24 (a tie) to even");
    dtof(0x3FF0000010000001ull, 0x3F800001u, "just above a tie rounds up");
    dtof(0x3FF000000FFFFFFFull, 0x3F800000u, "just below a tie rounds down");
    dtof(0xBFF0000010000001ull, 0xBF800001u, "negative");
    dtof(0x3FEFFFFFF0000000ull, 0x3F800000u, "rounds up into the next binade");
    dtof(0x47EFFFFFF0000000ull, 0x7F800000u, "rounds up past the largest: infinity");
    dtof(0x7FF0000000000000ull, 0x7F800000u, "infinity");
    dtof(0x7FF8000000000000ull, 0x7FB00000u, "dp-bit's NaN (as sqrtf(-1) gives on the PS2)");
    dtof(0x3810000000000000ull, 0x00800000u, "2^-126");
    dtof(0x380FFFFFFFFFFFFFull, 0x007FFFFFu, "below 2^-126: truncated, not rounded");
    dtof(0x0000000000000001ull, 0x00000000u, "a double denormal");
    dtof(0x8000000000000000ull, 0x80000000u, "-0");

    printf("%d of %d checks failed\n", failed, checked);
    return failed != 0;
}
