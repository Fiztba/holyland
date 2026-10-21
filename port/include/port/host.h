/* The PC port's view of the PlayStation 2: the types and macros the game code uses where it relied on
   the EE (32-bit pointers, game RAM, the scratchpad, the hardware registers). common.h includes this;
   docs/port/RULES.md says when to use each one. */
#ifndef PORT_HOST_H
#define PORT_HOST_H

#include <stddef.h>
#include <stdint.h>

/* ---- Integers that hold an address ----------------------------------------------------------
   On the EE a pointer fits in an int, so the game keeps addresses in s32/u32/int variables and
   fields. On the PC an address is 64 bits. A variable, parameter or runtime-struct field that holds
   an address becomes sptr (signed) or uptr (unsigned), which are pointer-sized. */
typedef intptr_t sptr;
typedef uintptr_t uptr;

/* ---- Game RAM and EE addresses --------------------------------------------------------------
   One block standing in for the EE's 32 MB of RAM (plus room for host growth). Its offsets are EE
   addresses: ee_ram + 0x5FAF80 is where the PS2 had the game's memory arena. Everything the game
   loads from disc lives in it, so a 32-bit word in a file can hold an address in it. The game's
   globals (in the program's image, not in game RAM) get EE addresses from 0x40000000 up
   (port/platform/ee_address.c), so a 32-bit word can point at them as well; the scratchpad keeps its
   PS2 address, 0x70000000. Only the stack and the C heap have no EE address. */
#define EE_RAM_SIZE 0x4000000u /* 64 MB: the PS2's 32 MB, and as much again for host growth */
extern unsigned char *ee_ram;

/* An EE address, kept in 32 bits: the field type of an "overlay" or "save" struct (one
   whose layout is fixed because it is read from disc or written to a save) where the PS2 kept a
   pointer. 0 is NULL. Write the pointed-to type in a comment: EEWord parts; (ModelPart *) */
typedef uint32_t EEWord;

/* The pointer an EEWord holds, as type T (a pointer type): EE_PTR(ModelPart *, header->parts). */
#define EE_PTR(T, word) ((T)ee_ptr_of((EEWord)(word)))
/* The EEWord for a pointer into game RAM or the game's globals (traps for any other pointer). */
#define EE_WORD(ptr) ee_word_of((const void *)(ptr), __FILE__, __LINE__)
/* A pointer at a fixed EE address in game RAM: EE_RAM_AT(char *, 0x5FAF80). */
#define EE_RAM_AT(T, ee_address) ((T)(ee_ram + (uint32_t)(ee_address)))

void *ee_static_ptr_of(EEWord word);
static inline void *ee_ptr_of(EEWord word) {
    if (word == 0) {
        return (void *)0;
    }
    return word < EE_RAM_SIZE ? (void *)(ee_ram + word) : ee_static_ptr_of(word);
}
EEWord ee_word_of(const void *ptr, const char *file, int line);
/* The same, without the trap: 1 and the EEWord, or 0 for a pointer with none (the stack, the heap). */
int ee_word_try(const void *ptr, EEWord *word);

/* ---- The scratchpad -------------------------------------------------------------------------
   The EE's 16 KB of fast RAM at 0x70000000. SPR(T, offset) is a pointer into it:
   ((f32 *)0x70000000) becomes SPR(f32 *, 0). */
extern unsigned char ee_scratchpad[0x4000];
/* its EE address, which EE_WORD gives and EE_PTR takes, as on the PS2 */
#define EE_SCRATCHPAD_ADDRESS 0x70000000u
#define SPR(T, offset) ((T)(ee_scratchpad + (offset)))

/* ---- Hardware registers ---------------------------------------------------------------------
   The EE's I/O registers (0x10000000..0x1000FFFF), the VU memories (0x11000000..0x1100FFFF) and the
   GS privileged registers (0x12000000..0x12001FFF) are plain memory on the PC: writes are kept,
   nothing reacts, and the host sets the few values the game waits on. EE_HW(T, address) is the
   register as a pointer: *(volatile int *)0x10003020 becomes *EE_HW(volatile int *, 0x10003020).
   The named registers are in eeregs.h. */
#define EE_HW(T, ee_address) ((T)ee_hw_ptr((uint32_t)(ee_address)))
void *ee_hw_ptr(uint32_t ee_address);

/* ---- Address spaces the PC does not have ----------------------------------------------------
   The EE reaches RAM uncached through 0x20000000|address and uncached-accelerated through
   0x30000000|address. On the PC both are the address itself. Use these instead of or-ing the bits
   into a pointer; PHYS() undoes them. */
#define UNCACHED(p) (p)
#define UNCACHED_ACCEL(p) (p)
#define PHYS(p) (p)

/* ---- VU0 macro mode -------------------------------------------------------------------------
   Inline asm that used VU0's registers is C on the PC. Where the game keeps values in VU0
   registers between calls (math/vu0_macro.c keeps matrices in vf1..vf14), they live here, so the
   converted code reads and writes the same "registers" in the same order. */
typedef struct {
    float vf[32][4] __attribute__((aligned(16))); /* vf0 is (0, 0, 0, 1) */
    int32_t vi[16];
    float acc[4]; /* ACC */
    float q;      /* Q, the result of div/sqrt/rsqrt */
    float i;      /* I */
} EeVu0;
extern EeVu0 ee_vu0;

/* ---- Not done yet ---------------------------------------------------------------------------
   PORT_TODO("...") logs once (with the place) that a PS2-only path was reached on the PC. Stubs
   and converted code use it where step 1 or later has to fill something in. */
void port_todo(const char *what, const char *file, int line);
#define PORT_TODO(what)                                                                            \
    do {                                                                                           \
        static int port_todo_seen_;                                                                \
        if (!port_todo_seen_) {                                                                    \
            port_todo_seen_ = 1;                                                                   \
            port_todo((what), __FILE__, __LINE__);                                                 \
        }                                                                                          \
    } while (0)

/* ---- Waiting on an interrupt handler ---------------------------------------------------------
   PORT_SPIN() is the body of a loop that waits, with no kernel call, for memory an interrupt handler
   changes. On the EE the interrupt comes while the loop spins; on the PC handlers run only in kernel
   calls (port/platform/kernel.c), so this lets EE time pass and runs the handlers that come due. No
   other thread of the same priority runs, as on the EE. */
void port_spin(void);
#define PORT_SPIN() port_spin()

/* Stops the game with a message: for states the PC port cannot go on from. */
void port_fatal(const char *file, int line, const char *fmt, ...) __attribute__((noreturn, format(printf, 3, 4)));
#define PORT_FATAL(...) port_fatal(__FILE__, __LINE__, __VA_ARGS__)

/* ---- PC presentation switches the game code reads -------------------------------------------
   Each is off by default (but port_whole_frame(), below), and the game code does exactly what it did
   on the PS2 while it is off.

   port_widescreen(): 1 while the opt-in 16:9 picture is on (--widescreen, or the overlay's Display
   page; port/platform/gs.c). The game's patches then widen the 3D projection, and tell the GS front
   end which of the 2D they draw by PATH2 is the HUD and which is centred 2D, with an A+D write to
   PORT_GS_REG_WIDESCREEN, a GS register address the GS does not have (the GS ignores it; with the
   switch off it is never sent). The GS front end squeezes that 2D horizontally to keep its 4:3 shape;
   everything drawn by PATH3 (the menus, the title) is 2D and squeezed without being told. */
int port_widescreen(void);
#define PORT_GS_REG_WIDESCREEN 0x7E
#define PORT_WIDESCREEN_3D 0      /* what follows is the 3D world: drawn as it comes (the default) */
#define PORT_WIDESCREEN_2D 1      /* 2D at 4:3, squeezed into the middle of the 16:9 picture */

/* port_whole_frame(): 1 while the game draws whole frames only (port_options.whole_frame). Unlike the
   switches above it is on by default, as one of the ways the PC picture differs from the GS's raw
   output (docs/port/PLAN.md); --no-whole-frame and --ps2-scanout turn it off, and the game code then
   does what it did on the PS2. On, GameLoop_DrawFrame leaves out Game_ShiftScreenByViewTurn: the
   frame before, moved by half the view's turn, drawn over the even lines (SCANMSK), a picture between
   for the field an interlaced TV shows next. On a PC screen, which shows both fields at once, it is a
   comb trailing what moves; --frame-rate 60 draws real pictures between instead. And the movie player
   (game/gfx/movie.c, MovieDisplay_Init) shows each decoded picture whole, in frame mode, instead of
   as two fields: a PC screen weaves a field with the one before, and the first field of each new
   picture with the last picture's other, a comb every other vblank. */
int port_whole_frame(void);
#define PORT_WIDESCREEN_HUD_LEFT 2 /* the HUD of the top left: at the 16:9 picture's left edge, or centred
                                      as PORT_WIDESCREEN_2D, by the player's choice */

/* port_frame_interp(): 1 while the opt-in higher frame rate is on (--frame-rate 60;
   port/platform/frame_interp.c). The patched GameLoop_DrawFrame then marks, in its packet chain, where
   the 3D world is and the cameras that drew it (game/frame_interp.c), with A+D writes to
   PORT_GS_REG_FRAME_INTERP, another address the GS does not have: bits 32-39 of the data are one of
   PORT_FRAME_INTERP_, bits 0-31 its value (a float's bits, or the header). The GS front end draws the
   world once more for the vblank in between, with the camera half way. */
int port_frame_interp(void);
#define PORT_GS_REG_FRAME_INTERP 0x7D
#define PORT_FRAME_INTERP_HEADER 0  /* last: projection | mode << 8 (GameLoop_SetCameraMatrices'), or _END */
#define PORT_FRAME_INTERP_VIEW 1    /* 1-12: world_view_matrix's columns' x, y, z */
#define PORT_FRAME_INTERP_SCREEN 13 /* 13-18: view_screen_matrix's [0], [5], [8], [9], [10], [14] */
#define PORT_FRAME_INTERP_END 0xFFFF /* the header after the world: what follows stays as it is */
#define PORT_FRAME_INTERP_ARM 0xFFFE /* the header before the arm: models in the world, moved as they move */

/* ---- Opt-in mods the game code reads ---------------------------------------------------------
   Each changes the game, so each is off by default; while it is off the game does what it did on
   the PS2.

   port_myus_necklace(): 1 while the Myu's Necklace mod is on (--myus-necklace, or the overlay's Mods
   page; port/platform/main_host.c). The map objects then glow as if the necklace were worn, whatever
   the amulet slot holds. */
int port_myus_necklace(void);

/* port_invincible(): 1 while the debug option is on (--invincible, or the overlay's Debug page; never
   saved). The player then loses no HP: hits still land, flash and knock back, but take nothing
   (Player_LoseHp), and falls, deadly ground and scripted kills or damage do not kill. For testing
   areas a new character could not survive; it is not a mod. */
int port_invincible(void);

/* Checks a struct's size and a field's offset against the PS2 layout written in its comments, for
   overlay and save structs: LAYOUT_SIZE(ModelHeader, 0x40); LAYOUT_OFFSET(ModelHeader, parts, 0x10); */
#define LAYOUT_SIZE(T, size) _Static_assert(sizeof(T) == (size), #T " is not its PS2 size")
#define LAYOUT_OFFSET(T, field, offset)                                                            \
    _Static_assert(offsetof(T, field) == (offset), #T "." #field " is not at its PS2 offset")

#endif /* PORT_HOST_H */
