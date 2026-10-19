/* Checks port/platform/vu1.c against hand-written microcode: VU float semantics, the flags and
   their 4-cycle delay, Q and P latencies, the branch's view of VI registers, and that an unknown
   instruction stops the program.

    out/<preset>/vu1_test        prints each check, and exits 1 if any failed

   Each test assembles a few instruction pairs into micro memory, sets up data memory, runs the
   program from 0 to its E bit and reads back what it stored. The program is linked with vu1.c alone;
   the few host functions it calls are defined here. */
#include <fenv.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"
#include "port/vu_float.h"

static uint8_t micro_mem[16384];
static uint8_t data_mem[16384];
static jmp_buf fatal_jump;
static int fatal_expected;
static int failures, checks;

void *ee_hw_ptr(uint32_t ee_address) {
    return ee_address == 0x11008000 ? (void *)micro_mem : (void *)data_mem;
}

void port_gif_path1_kick(uint32_t address) {
    (void)address;
}

PortOptions port_options; /* no --stats */
PortStats port_stats;

uint64_t port_ticks(void) {
    return 0;
}

void port_todo(const char *what, const char *file, int line) {
    printf("todo: %s (%s:%d)\n", what, file, line);
}

void port_fatal(const char *file, int line, const char *fmt, ...) {
    va_list args;

    (void)file;
    (void)line;
    if (!fatal_expected) {
        printf("FATAL: ");
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n");
    }
    longjmp(fatal_jump, 1);
}

/* ---- A small assembler -------------------------------------------------------------------------- */
enum { X = 8, Y = 4, Z = 2, W = 1, XYZW = 15 };

static int count;

static void pair(uint32_t upper, uint32_t lower) {
    memcpy(micro_mem + count * 8, &lower, 4);
    memcpy(micro_mem + count * 8 + 4, &upper, 4);
    count++;
}

/* upper: op 0x00-0x2F (fd), and the 0x3C-0x3F table by its index (ACC, ITOF, CLIP ...) */
static uint32_t up(int op, int dest, int ft, int fs, int fd) {
    return (uint32_t)(dest << 21 | ft << 16 | fs << 11 | fd << 6 | op);
}
static uint32_t up2(int index, int dest, int ft, int fs) {
    return (uint32_t)(dest << 21 | ft << 16 | fs << 11 | (index >> 2) << 6 | 0x3C | (index & 3));
}
#define UNOP up2(0x2F, 0, 0, 0)
#define U_ADD 0x28
#define U_SUB 0x2C
#define U_MUL 0x2A
#define U_MAX 0x2B
#define U_MINI 0x2F
#define U_ADDQ 0x20
#define U_OPMSUB 0x2E

/* lower: by its top 7 bits, by op 0x30-0x35 of 0x40 (IADD ...), and the 0x40 table by its index */
static uint32_t lo(int top, int dest, int it, int is, int imm) {
    return (uint32_t)(top << 25 | dest << 21 | it << 16 | is << 11 | (imm & 0x7FF));
}
static uint32_t lo2(int index, int dest, int it, int is) {
    return 0x80000000u | (uint32_t)(dest << 21 | it << 16 | is << 11 | (index >> 2) << 6 | 0x3C | (index & 3));
}
#define LNOP lo2(0x30, 0, 0, 0)
#define LQ(dest, ft, is, imm) lo(0x00, dest, ft, is, imm)
#define SQ(dest, fs, it, imm) lo(0x01, dest, it, fs, imm)
#define ISW(dest, it, is, imm) lo(0x05, dest, it, is, imm)
#define ILW(dest, it, is, imm) lo(0x04, dest, it, is, imm)
#define IADDIU(it, is, imm) lo(0x08, ((imm) >> 11) & 15, it, is, (imm) & 0x7FF)
#define ISUBIU(it, is, imm) lo(0x09, ((imm) >> 11) & 15, it, is, (imm) & 0x7FF)
#define IBNE(it, is, off) lo(0x29, 0, it, is, off)
#define IBEQ(it, is, off) lo(0x28, 0, it, is, off)
#define DIV(fs, fsf, ft, ftf) lo2(0x38, (ftf) << 2 | (fsf), ft, fs)
#define SQRT(ft, ftf) lo2(0x39, (ftf) << 2, ft, 0)
#define RSQRT(fs, fsf, ft, ftf) lo2(0x3A, (ftf) << 2 | (fsf), ft, fs)
#define WAITQ lo2(0x3B, 0, 0, 0)
#define WAITP lo2(0x7B, 0, 0, 0)
#define MFP(dest, ft) lo2(0x64, dest, ft, 0)
#define LQI(dest, ft, is) lo2(0x34, dest, ft, is)
#define MR32(dest, ft, fs) lo2(0x31, dest, ft, fs)
#define FCSET(imm) (0x11u << 25 | (uint32_t)(imm))
#define FCAND(imm) (0x12u << 25 | (uint32_t)(imm))
#define FMAND(it, is) lo(0x1A, 0, it, is, 0)
#define FSAND(it, imm) lo(0x16, ((imm) >> 11) & 1, it, 0, (imm) & 0x7FF)
#define FSSET(imm) lo(0x15, ((imm) >> 11) & 1, 0, 0, (imm) & 0x7FF)
#define EFU(index, dest, fs) lo2(index, dest, 0, fs)
#define E_BIT 0x40000000u

static void begin(void) {
    port_vu1_reset();
    memset(micro_mem, 0, sizeof(micro_mem));
    memset(data_mem, 0, sizeof(data_mem));
    count = 0;
}

static void set_qword(int address, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    uint32_t q[4] = {x, y, z, w};

    memcpy(data_mem + address * 16, q, 16);
}

static uint32_t word(int address, int field) {
    uint32_t v;

    memcpy(&v, data_mem + address * 16 + field * 4, 4);
    return v;
}

static float as_f(uint32_t bits) {
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

static uint32_t f_bits(float f) {
    uint32_t bits;

    memcpy(&bits, &f, 4);
    return bits;
}

/* runs the program, ending it with an E bit; returns 1 if it stopped with PORT_FATAL */
static int run(void) {
    pair(UNOP | E_BIT, LNOP);
    pair(UNOP, LNOP);
    port_vu1_micro_written();
    if (setjmp(fatal_jump)) {
        fesetenv(FE_DFL_ENV); /* run() set round toward zero (and DAZ) */
        return 1;
    }
    port_vu1_start(0, 0, 0);
    return 0;
}

static void nops(int n) {
    while (n-- > 0) {
        pair(UNOP, LNOP);
    }
}

static void check(const char *what, uint32_t got, uint32_t want) {
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %-52s got %08X, want %08X\n", what, got, want);
    } else {
        printf("ok   %s\n", what);
    }
}

static void check_near(const char *what, uint32_t got, float want, float tolerance) {
    checks++;
    if (!(fabsf(as_f(got) - want) <= tolerance)) {
        failures++;
        printf("FAIL %-52s got %g, want %g\n", what, as_f(got), want);
    } else {
        printf("ok   %s\n", what);
    }
}

/* ---- The tests ---------------------------------------------------------------------------------- */
/* a new program at 0, VU1's registers and data memory as the last one left them */
static void reupload(void) {
    memset(micro_mem, 0, sizeof(micro_mem));
    count = 0;
}

/* stores the MAC flag at qword 2 x and the status flag at qword 2 y */
static void read_flags(void) {
    pair(UNOP, ISUBIU(2, 0, 1));
    pair(UNOP, FMAND(3, 2));
    pair(UNOP, FSAND(4, 0xFFF));
    pair(UNOP, ISW(X, 3, 0, 2));
    pair(UNOP, ISW(Y, 4, 0, 2));
    run();
}

/* vf1 = qword 0, vf2 = qword 1, loaded and waited for */
static void load12(void) {
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    pair(UNOP, LQ(XYZW, 2, 0, 1));
}

static void test_float(void) {
    begin();
    set_qword(0, 0x3F800000u, 0x3F800000u, 0x7F800000u, 0x7FFFFFFFu);
    set_qword(1, 0x34400000u, 0xB3A00000u, 0x3F000000u, 0x40000000u);
    load12();
    pair(up(U_ADD, X | Y, 2, 1, 3), LNOP);   /* 1 + 1.5 * 2^-23, 1 - 1.25 * 2^-24 */
    pair(up(U_MUL, Z | W, 2, 1, 3), LNOP);   /* 2^128 * 0.5, max * 2 */
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    run();
    check("add rounds toward zero (1 + 1.5 * 2^-23)", word(2, 0), 0x3F800001u);
    check("add rounds toward zero (1 - 1.25 * 2^-24)", word(2, 1), 0x3F7FFFFEu);
    check("exponent 255 is a number (2^128 * 0.5)", word(2, 2), 0x7F000000u);
    check("overflow gives the largest float", word(2, 3), 0x7FFFFFFFu);

    begin();
    set_qword(0, 0x00000001u, 0x80400000u, 0x00800000u, 0x3F800000u);
    set_qword(1, 0x00000000u, 0x3F800000u, 0x3F000000u, 0x00000000u);
    load12();
    pair(up(U_ADD, X, 2, 1, 3), LNOP);       /* denormal + 0 */
    pair(up(U_MUL, Y | Z, 2, 1, 3), LNOP);   /* -denormal * 1, 2^-126 * 0.5 */
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    run();
    check("a denormal reads as zero", word(2, 0), 0x00000000u);
    check("a negative denormal reads as -0", word(2, 1), 0x80000000u);
    check("underflow gives zero", word(2, 2), 0x00000000u);
}

static void test_minmax_clip(void) {
    begin();
    set_qword(0, 0x80000000u, 0x00000000u, 0xBF800000u, 0x3F800000u);
    set_qword(1, 0x00000000u, 0x80000000u, 0xC0000000u, 0x7F800001u);
    load12();
    pair(up(U_MAX, XYZW, 2, 1, 3), LNOP);
    pair(up(U_MINI, XYZW, 2, 1, 4), LNOP);
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    pair(UNOP, SQ(XYZW, 4, 0, 3));
    run();
    check("max(-0, +0) is +0", word(2, 0), 0x00000000u);
    check("max(+0, -0) is +0", word(2, 1), 0x00000000u);
    check("max(-1, -2) is -1", word(2, 2), 0xBF800000u);
    check("max compares exponent 255 as a number", word(2, 3), 0x7F800001u);
    check("mini(-0, +0) is -0", word(3, 0), 0x80000000u);
    check("mini(-1, -2) is -2", word(3, 2), 0xC0000000u);

    /* CLIP: x > +w, y < -w, z inside; then FCAND sees it only 4 cycles on */
    begin();
    set_qword(0, 0x40000000u, 0xC0000000u, 0x3F000000u, 0x3F800000u);
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    nops(3);
    pair(up2(0x1F, 0xE, 1, 1), LNOP);        /* clip.xyz vf1, vf1w */
    pair(UNOP, FCAND(0x3F));                 /* 1 cycle on: not yet */
    pair(UNOP, ISW(X, 1, 0, 2));
    pair(UNOP, LNOP);
    pair(UNOP, FCAND(0x09));                 /* 4 cycles on: +x and -y */
    pair(UNOP, ISW(Y, 1, 0, 2));
    pair(UNOP, FCSET(0x000004));
    pair(UNOP, FCAND(0x04));                 /* FCSET is delayed as CLIP is */
    pair(UNOP, ISW(Z, 1, 0, 2));
    nops(2);
    pair(UNOP, FCAND(0x04));
    pair(UNOP, ISW(W, 1, 0, 2));
    run();
    check("FCAND before the CLIP is due", word(2, 0), 0);
    check("FCAND sees CLIP 4 cycles on (+x, -y)", word(2, 1), 1);
    check("FCAND before FCSET is due", word(2, 2), 0);
    check("FCAND sees FCSET 4 cycles on", word(2, 3), 1);
}

static void test_flags(void) {
    /* sub.x vf3 = vf1 - vf1 = 0: Z for x (MAC bit 3), status Z and ZS */
    begin();
    set_qword(0, 0x3F800000u, 0xBF800000u, 0, 0);
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    nops(3);
    pair(up(U_SUB, X | Y, 0, 1, 3), ISUBIU(2, 0, 1)); /* x: 1 - 0 = 1, y: -1 - 0 */
    pair(up(U_SUB, X, 1, 1, 3), FMAND(3, 2));              /* x: 0; MAC not yet set */
    nops(2);
    pair(UNOP, FMAND(4, 2));                               /* the first SUB's flags: S for y */
    pair(UNOP, FMAND(5, 2));                               /* the second's: Z for x */
    pair(UNOP, FSAND(6, 0xFFF));
    pair(up(U_ADD, X, 0, 1, 3), FSSET(0));                 /* x: 1 - nonzero, clear sticky */
    nops(4);
    pair(UNOP, FSAND(7, 0xFFF));
    pair(UNOP, ISW(X, 3, 0, 2));
    pair(UNOP, ISW(Y, 4, 0, 2));
    pair(UNOP, ISW(Z, 5, 0, 2));
    pair(UNOP, ISW(W, 6, 0, 2));
    pair(UNOP, ISW(X, 7, 0, 3));
    run();
    check("FMAND before the flags are due", word(2, 0), 0x0000);
    check("MAC flag: S for y", word(2, 1), 0x0040);
    check("MAC flag: Z for x, other fields cleared", word(2, 2), 0x0008);
    check("status: Z, ZS and SS (sticky)", word(2, 3), 0x00C1);
    check("status after FSSET 0 and a nonzero add", word(3, 0), 0x0000);

    /* overflow and underflow flags */
    begin();
    set_qword(0, 0x7F000000u, 0x00800000u, 0, 0);
    set_qword(1, 0x7F000000u, 0x00800000u, 0, 0);
    load12();
    pair(up(U_MUL, X | Y, 2, 1, 3), ISUBIU(2, 0, 1));
    nops(3);
    pair(UNOP, FMAND(3, 2));
    pair(UNOP, FSAND(4, 0xFFF));
    pair(UNOP, ISW(X, 3, 0, 2));
    pair(UNOP, ISW(Y, 4, 0, 2));
    run();
    check("MAC flag: O for x, U and Z for y", word(2, 0), 0x8404);
    check("status: Z U O and their sticky bits", word(2, 1), 0x034D);

    /* A program that reads no flags leaves them as one that does would (vu1.c skips the flag
       pipeline then), for the next program to read: S for y, O for z, U and Z for w */
    begin();
    set_qword(0, 0x3F800000u, 0xBF800000u, 0x7F000000u, 0x00800000u);
    set_qword(1, 0x3F800000u, 0x3F800000u, 0x7F000000u, 0x00800000u);
    load12();
    pair(up(U_MUL, XYZW, 2, 1, 3), LNOP);
    run();
    reupload();
    read_flags();
    check("flags left by a program that reads none: MAC", word(2, 0), 0x2141);
    check("flags left by a program that reads none: status", word(2, 1), 0x03CF);
    reupload();
    pair(up(U_ADD, X, 0, 1, 4), LNOP); /* 1 + 0: no flags, but the sticky bits stay */
    run();
    reupload();
    read_flags();
    check("a later program's flags, and the sticky bits: MAC", word(2, 0), 0x0000);
    check("a later program's flags, and the sticky bits: status", word(2, 1), 0x03C0);
}

static void test_q(void) {
    begin();
    set_qword(0, 0x3F800000u, 0x00000000u, 0x40800000u, 0xC0800000u);
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    nops(3);
    pair(UNOP, DIV(1, 0, 1, 2));             /* 1 / 4 */
    nops(5);
    pair(up(U_ADDQ, X, 0, 0, 3), LNOP);      /* 6 cycles on: the old Q (0) */
    pair(up(U_ADDQ, Y, 0, 0, 3), LNOP);      /* 7 cycles on: 0.25 */
    pair(UNOP, DIV(1, 0, 1, 1));             /* 1 / 0 */
    pair(UNOP, WAITQ);
    pair(up(U_ADDQ, Z, 0, 0, 3), FSAND(4, 0xFFF));
    pair(UNOP, SQRT(1, 3));                  /* sqrt(-4): 2, and I */
    pair(UNOP, WAITQ);
    pair(up(U_ADDQ, W, 0, 0, 3), FSAND(5, 0xFFF));
    pair(UNOP, DIV(1, 1, 1, 1));             /* 0 / 0 */
    pair(UNOP, WAITQ);
    pair(up(U_ADDQ, X, 0, 0, 4), FSAND(6, 0xFFF));
    pair(UNOP, RSQRT(1, 0, 1, 2));           /* 1 / sqrt(4) */
    pair(UNOP, WAITQ);
    pair(up(U_ADDQ, Y, 0, 0, 4), LNOP);
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    pair(UNOP, SQ(XYZW, 4, 0, 3));
    pair(UNOP, ISW(X, 4, 0, 4));
    pair(UNOP, ISW(Y, 5, 0, 4));
    pair(UNOP, ISW(Z, 6, 0, 4));
    run();
    check("Q before DIV is due (6 cycles)", word(2, 0), 0x00000000u);
    check("Q when DIV is due (7 cycles)", word(2, 1), 0x3E800000u);
    check("1/0 gives the largest float", word(2, 2), 0x7FFFFFFFu);
    check("sqrt(-4) is 2 (plus vf0.w)", word(2, 3), 0x40400000u);
    check("1/0 sets D and DS", word(4, 0) & 0xC30, 0x820);
    check("sqrt(-4) sets I and IS, keeps DS", word(4, 1) & 0xC30, 0xC10);
    check("0/0 gives the largest float", word(3, 0), 0x7FFFFFFFu);
    check("0/0 sets I, not D", word(4, 2) & 0x30, 0x10);
    check("rsqrt 1/sqrt(4)", word(3, 1), 0x3F000000u);
}

static void test_p(void) {
    /* ESUM is 12 cycles: MFP 11 cycles on still reads the old P */
    begin();
    set_qword(0, 0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u);
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    nops(3);
    pair(UNOP, EFU(0x76, XYZW, 1));          /* esum: 10 */
    nops(10);
    pair(UNOP, MFP(X, 2));                   /* 11 on */
    pair(UNOP, MFP(Y, 2));                   /* 12 on */
    pair(UNOP, EFU(0x70, 0xE, 1));           /* esadd: 1 + 4 + 9 */
    pair(UNOP, WAITP);
    pair(UNOP, MFP(Z, 2));
    nops(3);
    pair(UNOP, SQ(XYZW, 2, 0, 2));
    run();
    check("MFP before ESUM is due (11 cycles)", word(2, 0), 0);
    check("ESUM when due (12 cycles)", word(2, 1), 0x41200000u);
    check("ESADD after WAITP", word(2, 2), 0x41600000u);

    /* ESUM after another EFU op waits for it (so a pending DIV is due by then) */
    begin();
    set_qword(0, 0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u);
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    nops(3);
    pair(UNOP, DIV(1, 0, 1, 1));             /* q = 0.5, due in 7 */
    pair(UNOP, EFU(0x70, 0xE, 1));
    pair(UNOP, EFU(0x76, XYZW, 1));          /* stalls 9 cycles */
    pair(up(U_ADDQ, X, 0, 0, 3), LNOP);
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    run();
    check("ESUM waits for the EFU", word(2, 0), 0x3F000000u);

    /* the series ops, roughly */
    begin();
    set_qword(0, 0x3F000000u, 0x3F800000u, 0x3F800000u, 0x40000000u);
    pair(UNOP, LQ(XYZW, 1, 0, 0));
    nops(3);
    pair(UNOP, EFU(0x7C, 0, 1));             /* esin(0.5) */
    pair(UNOP, WAITP);
    pair(UNOP, MFP(X, 2));
    pair(UNOP, EFU(0x7D, 1, 1));             /* eatan(1) */
    pair(UNOP, WAITP);
    pair(UNOP, MFP(Y, 2));
    pair(UNOP, EFU(0x7E, 2, 1));             /* eexp(1): e^-1 */
    pair(UNOP, WAITP);
    pair(UNOP, MFP(Z, 2));
    pair(UNOP, EFU(0x74, XYZW, 1));          /* eatanxy: atan(1/0.5) */
    pair(UNOP, WAITP);
    pair(UNOP, MFP(W, 2));
    nops(3);
    pair(UNOP, SQ(XYZW, 2, 0, 2));
    run();
    check_near("ESIN(0.5)", word(2, 0), 0.4794255f, 1e-5f);
    check_near("EATAN(1)", word(2, 1), 0.7853982f, 1e-5f);
    check_near("EEXP(1)", word(2, 2), 0.3678794f, 1e-4f);
    check_near("EATANxy(0.5, 1)", word(2, 3), 1.1071487f, 1e-4f);
}

static void test_convert(void) {
    begin();
    set_qword(0, f_bits(1.75f), f_bits(-1.75f), 0x7F000000u, 0xFF7FFFFFu);
    set_qword(1, 0x00000010u, 0xFFFFFFF0u, 0x00008000u, 0x7FFFFFFFu);
    load12();
    pair(up2(0x15, XYZW, 3, 1), LNOP);       /* ftoi4 */
    pair(up2(0x11, XYZW, 4, 2), LNOP);       /* itof4 */
    pair(up2(0x13, Z, 5, 2), LNOP);          /* itof15 */
    pair(up2(0x10, W, 5, 2), LNOP);          /* itof0 */
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    pair(UNOP, SQ(XYZW, 4, 0, 3));
    pair(UNOP, SQ(XYZW, 5, 0, 4));
    run();
    check("ftoi4(1.75)", word(2, 0), 28);
    check("ftoi4(-1.75)", word(2, 1), (uint32_t)-28);
    check("ftoi4 saturates up", word(2, 2), 0x7FFFFFFFu);
    check("ftoi4 saturates down", word(2, 3), 0x80000000u);
    check("itof4(16)", word(3, 0), 0x3F800000u);
    check("itof4(-16)", word(3, 1), 0xBF800000u);
    check("itof15(0x8000)", word(4, 2), 0x3F800000u);
    check("itof0 rounds toward zero", word(4, 3), 0x4EFFFFFFu);
}

static void test_shuffles(void) {
    begin();
    set_qword(0, f_bits(1.0f), f_bits(2.0f), f_bits(3.0f), f_bits(4.0f));
    set_qword(1, f_bits(5.0f), f_bits(6.0f), f_bits(7.0f), f_bits(8.0f));
    load12();
    pair(UNOP, MR32(XYZW, 3, 1));
    pair(up2(0x2E, 0xE, 2, 1), LNOP);        /* opmula: acc = vf1.yzx * vf2.zxy */
    pair(up(U_OPMSUB, 0xE, 1, 2, 4), LNOP);  /* vf4 = acc - vf2.yzx * vf1.zxy: the cross product */
    nops(3);
    pair(UNOP, SQ(XYZW, 3, 0, 2));
    pair(UNOP, SQ(0xE, 4, 0, 3));
    run();
    check("mr32.x", word(2, 0), f_bits(2.0f));
    check("mr32.w", word(2, 3), f_bits(1.0f));
    check("opmula/opmsub cross x", word(3, 0), f_bits(2 * 7 - 3 * 6));
    check("opmula/opmsub cross y", word(3, 1), f_bits(3 * 5 - 1 * 7));
    check("opmula/opmsub cross z", word(3, 2), f_bits(1 * 6 - 2 * 5));
}

/* port/vu_float.h, the arithmetic of the EE's VU0 macro-mode instructions (the game's math/vu0.h),
   gives the bits VU1 gives: ADD, SUB, MUL, DIV, FTOI4 and ITOF4 on random operands, with exponents
   near each other and far apart, denormals and exponent 255. The game projects some vertices on VU0
   and the same ones again on VU1, and depth-tests one pass against the other. */
static uint32_t rng_state = 0x2545F491u;

static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* a float with an exponent near `near` most of the time, else anywhere (0, 255 and denormals too) */
static uint32_t random_float(uint32_t near) {
    uint32_t r = rng(), e;

    switch (r & 7) {
    case 0: e = (rng() >> 8) & 0xFF; break;
    case 1: e = (r >> 3) & 1 ? 0 : 255; break;
    default: e = near + ((r >> 3) & 63) - 32; break;
    }
    return (rng() & 0x807FFFFFu) | (e & 0xFF) << 23;
}

static void test_vu0_float(void) {
    int round, lane, bad_add = 0, bad_sub = 0, bad_mul = 0, bad_div = 0, bad_ftoi = 0, bad_itof = 0;

    for (round = 0; round < 3000; round++) {
        uint32_t a[4], b[4], n[4];

        for (lane = 0; lane < 4; lane++) {
            uint32_t near = 96 + rng() % 64;

            a[lane] = random_float(near);
            b[lane] = random_float(near);
            n[lane] = rng() >> (rng() & 31);
        }
        begin();
        set_qword(0, a[0], a[1], a[2], a[3]);
        set_qword(1, b[0], b[1], b[2], b[3]);
        set_qword(2, n[0], n[1], n[2], n[3]);
        load12();
        pair(UNOP, LQ(XYZW, 7, 0, 2));
        pair(up(U_ADD, XYZW, 2, 1, 3), LNOP);
        pair(up(U_SUB, XYZW, 2, 1, 4), LNOP);
        pair(up(U_MUL, XYZW, 2, 1, 5), DIV(1, 0, 2, 0));  /* a.x / b.x */
        pair(up2(0x15, XYZW, 6, 1), WAITQ);             /* ftoi4(a) */
        pair(up(0x1C, W, 0, 0, 8), LNOP);                /* mulq: vf0.w (1) * Q, which keeps -0 */
        pair(up2(0x11, XYZW, 9, 7), LNOP);              /* itof4(n) */
        nops(3);
        pair(UNOP, SQ(XYZW, 3, 0, 3));
        pair(UNOP, SQ(XYZW, 4, 0, 4));
        pair(UNOP, SQ(XYZW, 5, 0, 5));
        pair(UNOP, SQ(XYZW, 6, 0, 6));
        pair(UNOP, SQ(XYZW, 8, 0, 7));
        pair(UNOP, SQ(XYZW, 9, 0, 8));
        run();
        for (lane = 0; lane < 4; lane++) {
            int add = word(3, lane) != vu_float_add(a[lane], b[lane]);
            int sub = word(4, lane) != vu_float_sub(a[lane], b[lane]);
            int mul = word(5, lane) != vu_float_mul(a[lane], b[lane]);

            if ((add || sub || mul) && bad_add + bad_sub + bad_mul == 0) {
                printf("     the first that differs: %08X, %08X: add %08X (VU1) %08X, sub %08X %08X, mul %08X %08X\n",
                       a[lane], b[lane], word(3, lane), vu_float_add(a[lane], b[lane]), word(4, lane),
                       vu_float_sub(a[lane], b[lane]), word(5, lane), vu_float_mul(a[lane], b[lane]));
            }
            bad_add += add;
            bad_sub += sub;
            bad_mul += mul;
            bad_ftoi += word(6, lane) != (uint32_t)vu_float_ftoi(a[lane], 4);
            bad_itof += word(8, lane) != vu_float_itof((int32_t)n[lane], 4);
        }
        if (word(7, 3) != vu_float_div(a[0], b[0]) && bad_div++ == 0) {
            printf("     the first divide that differs: %08X / %08X: %08X (VU1) %08X\n", a[0], b[0], word(7, 3),
                   vu_float_div(a[0], b[0]));
        }
    }
    check("VU0's add is VU1's (12000 random pairs)", (uint32_t)bad_add, 0);
    check("VU0's sub is VU1's", (uint32_t)bad_sub, 0);
    check("VU0's mul is VU1's", (uint32_t)bad_mul, 0);
    check("VU0's div is VU1's (3000)", (uint32_t)bad_div, 0);
    check("VU0's ftoi4 is VU1's", (uint32_t)bad_ftoi, 0);
    check("VU0's itof4 is VU1's", (uint32_t)bad_itof, 0);
    /* the cases the double path cannot do: exponents more than 28 apart */
    check("1 - 2^-40 rounds toward zero", vu_float_sub(0x3F800000u, 0x2B800000u), 0x3F7FFFFFu);
    check("1 + 2^-40 is 1", vu_float_add(0x3F800000u, 0x2B800000u), 0x3F800000u);
    check("-2^100 + 2^60 rounds toward zero", vu_float_add(0xF1800000u, 0x5D800000u), 0xF17FFFFFu);
}

/* A branch right after an integer write sees the old value; one instruction later, the new one. */
static void test_branch_vi(void) {
    begin();
    pair(UNOP, IADDIU(1, 0, 1));
    pair(UNOP, IBNE(1, 0, 2));               /* sees vi1 = 0: not taken */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(2, 0, 1));             /* vi2 = 1 if not taken */
    pair(UNOP, IADDIU(3, 0, 1));
    pair(UNOP, LNOP);
    pair(UNOP, IBNE(3, 0, 2));               /* sees vi3 = 1: taken */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(4, 0, 1));             /* skipped */
    pair(UNOP, LNOP);
    pair(UNOP, ISW(X, 2, 0, 2));
    pair(UNOP, ISW(Y, 4, 0, 2));
    run();
    check("branch after IADDIU sees the old value", word(2, 0), 1);
    check("branch one instruction later sees the new one", word(2, 1), 0);

    /* writes within 4 cycles of the branch are all unseen when the last is just before it */
    begin();
    pair(UNOP, IADDIU(2, 0, 5));
    pair(UNOP, IADDIU(1, 0, 5));
    nops(4);
    pair(UNOP, IADDIU(1, 1, 1));             /* 6, 3 cycles before the branch */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(1, 1, 1));             /* 7, just before it */
    pair(UNOP, IBEQ(1, 2, 2));               /* sees vi1 = 5 */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(3, 0, 1));             /* skipped if taken */
    pair(UNOP, LNOP);
    pair(UNOP, ISW(X, 3, 0, 2));
    run();
    check("branch sees the value from 4 cycles back", word(2, 0), 0);

    /* LQI's step, and FCAND (seen at once), and ILW (the branch stalls for it) */
    begin();
    set_qword(3, 7, 0, 0, 0);
    pair(UNOP, LQI(XYZW, 5, 1));             /* vi1: 0 -> 1 */
    pair(UNOP, IBNE(1, 0, 2));               /* sees 0: not taken */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(2, 0, 1));
    pair(UNOP, FCAND(0xFFFFFF));             /* vi1 = 0 */
    pair(UNOP, IBEQ(1, 0, 2));               /* sees 0: taken */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(3, 0, 1));             /* skipped */
    pair(UNOP, ILW(X, 4, 0, 3));             /* vi4 = 7 */
    pair(UNOP, IBNE(4, 0, 2));               /* waits for it: taken */
    pair(UNOP, LNOP);
    pair(UNOP, IADDIU(5, 0, 1));             /* skipped */
    pair(UNOP, LNOP);
    pair(UNOP, ISW(X, 2, 0, 2));
    pair(UNOP, ISW(Y, 3, 0, 2));
    pair(UNOP, ISW(Z, 5, 0, 2));
    run();
    check("branch after LQI sees the old address", word(2, 0), 1);
    check("branch after FCAND sees its result", word(2, 1), 0);
    check("branch after ILW waits for the load", word(2, 2), 0);
}

static void test_unknown(void) {
    begin();
    pair(up(0x30, 0, 0, 0, 0), LNOP);        /* no upper op 0x30 */
    fatal_expected = 1;
    check("an unknown upper instruction is fatal", (uint32_t)run(), 1);
    begin();
    pair(UNOP, lo(0x7F, 0, 0, 0, 0));
    check("an unknown lower instruction is fatal", (uint32_t)run(), 1);
    begin();
    pair(UNOP, lo2(0x77, 0, 0, 0));          /* between EATANxz and ESQRT */
    check("an unknown 0x40-table instruction is fatal", (uint32_t)run(), 1);
    fatal_expected = 0;
}

int main(void) {
    test_float();
    test_minmax_clip();
    test_flags();
    test_q();
    test_p();
    test_convert();
    test_shuffles();
    test_vu0_float();
    test_branch_vi();
    test_unknown();
    printf("%d of %d checks failed\n", failures, checks);
    return failures != 0;
}
