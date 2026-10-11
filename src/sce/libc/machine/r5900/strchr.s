# libc.a member strchr.o, assembled from newlib's
# KFIV: the section is 8-byte aligned (.align 3); the link places it at 0x242c88.
# src/newlib/libc/machine/r5900/strchr.S. It scans a quadword (or a
# doubleword) at a time, looking for the character and for a zero byte with
# the MMI psubb/pnor idiom, and finishes byte by byte. The last byte compared
# is returned; the result is the first match, or NULL.
    .section .text
    .set noat
    .set noreorder
    .global strchr
    .type strchr, @function
    .align 3
strchr:
    andi       $2, $4, 0x7
    bnez       $2, .L00242DF8
    andi       $5, $5, 0xFF
    dsll       $3, $5, 8
    lui        $6, 0x101
    ori        $6, $6, 0x101
    dsll       $6, $6, 16
    ori        $6, $6, 0x101
    dsll       $6, $6, 16
    ori        $6, $6, 0x101
    daddu      $10, $3, $5
    andi       $3, $4, 0xF
    dsll       $2, $10, 16
    lui        $8, 0x8080
    ori        $8, $8, 0x8080
    dsll       $8, $8, 16
    ori        $8, $8, 0x8080
    dsll       $8, $8, 16
    ori        $8, $8, 0x8080
    daddu      $2, $2, $10
    dsll32     $10, $2, 0
    bnez       $3, .L00242D70
    daddu      $7, $2, $10
    .align 2
.L00242CE4:
    lq         $9, 0x0($4)
    pcpyld     $10, $6, $6
    pnor       $3, $0, $9
    psubb      $2, $9, $10
    pcpyld     $6, $8, $8
    pand       $2, $2, $3
    pcpyld     $8, $7, $7
    pand       $2, $2, $6
    pcpyud     $3, $2, $7
    or         $3, $2, $3
    bnel       $3, $0, .L00242DFC
    lbu        $2, 0x0($4)
    pxor       $2, $9, $8
    psubb      $3, $2, $10
    pnor       $2, $0, $2
    lui        $8, 0x8080
    ori        $8, $8, 0x8080
    dsll       $8, $8, 16
    ori        $8, $8, 0x8080
    dsll       $8, $8, 16
    ori        $8, $8, 0x8080
    pand       $3, $3, $2
    pand       $3, $3, $6
    lui        $6, 0x101
    ori        $6, $6, 0x101
    dsll       $6, $6, 16
    ori        $6, $6, 0x101
    dsll       $6, $6, 16
    ori        $6, $6, 0x101
    pcpyud     $2, $3, $5
    or         $3, $2, $3
    beql       $3, $0, .L00242CE4
    addiu      $4, $4, 0x10
    b          .L00242DFC
    lbu        $2, 0x0($4)
    .align 2
.L00242D70:
    ld         $9, 0x0($4)
    nor        $3, $0, $9
    dsubu      $2, $9, $6
    and        $2, $2, $3
    and        $2, $2, $8
    bnel       $2, $0, .L00242DFC
    lbu        $2, 0x0($4)
    xor        $2, $9, $7
    dsubu      $3, $2, $6
    nor        $2, $0, $2
    and        $3, $3, $2
    and        $3, $3, $8
    bnel       $3, $0, .L00242DFC
    lbu        $2, 0x0($4)
    daddu      $9, $6, $0
    addiu      $4, $4, 0x8
    .align 2
.L00242DB0:
    ld         $6, 0x0($4)
    dsubu      $2, $6, $9
    nor        $3, $0, $6
    and        $2, $2, $3
    and        $2, $2, $8
    bnez       $2, .L00242DF8
    xor        $2, $6, $7
    nor        $3, $0, $2
    dsubu      $2, $2, $9
    and        $2, $2, $3
    and        $2, $2, $8
    beql       $2, $0, .L00242DB0
    addiu      $4, $4, 0x8
    b          .L00242DFC
    lbu        $2, 0x0($4)
    .align 2
.L00242DEC:
    beql       $2, $5, .L00242E08
    lbu        $3, 0x0($4)
    addiu      $4, $4, 0x1
    .align 2
.L00242DF8:
    lbu        $2, 0x0($4)
    .align 2
.L00242DFC:
    bnez       $2, .L00242DEC
    nop
    lbu        $3, 0x0($4)
    .align 2
.L00242E08:
    daddu      $2, $0, $0
    xor        $3, $3, $5
    jr         $31
    movz       $2, $4, $3
    .size strchr, . - strchr
    .set reorder
    .set at
