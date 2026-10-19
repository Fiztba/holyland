/* PC controls (port/include/port/controls.h): the keyboard and mouse layout of Verdite, the KF2/KF3
   ports, opt-in with --controls verdite. Without it the pad and keyboard are as port/sdk/libpad.c
   has them, and nothing here runs.

   Keyboard:  W / Up        d-pad up (forward)       S / Down     d-pad down (back)
              Left / Right  d-pad left / right       A / D        L1 / R1 (side steps)
              Space         square (attack)          F            cross
              Q             triangle                 Tab          circle
              Enter         start                    Right Shift  select
              Escape        capture / release the mouse
   Mouse, captured, while the player is in control: moving turns and looks; left square, right
   triangle, middle cross. Otherwise (menus, pause, dialogue): left cross, right circle, middle
   cross, the wheel up / down.

   Mouse look turns the player in Player_Move, where the game adds the pad's turning (port_mouse_look):
   each movement is applied once, so it neither coasts nor builds speed. Capture is manual (Escape),
   as in Verdite; losing the window's focus releases it. A small mouse in the corner shows the change.
   A pad script (--input) turns all of this off, so scripted runs stay the same. */
#include <string.h>

#include "port/controls.h"
#include "port/host.h"
#include "port/platform.h"

#ifdef KF4_HAVE_SDL
#include <SDL.h>
#endif

#define PI 3.14159265358979323846
#define RADIANS_PER_PIXEL (0.15 * PI / 180.0)
#define MAX_TURN (PI / 2.0)

static struct {
    int enabled;
    int captured;
    int focused;
    double yaw;                  /* mouse movement not yet applied, in radians */
    double pitch;
    uint64_t look_vblank;        /* the vblank of the game's last call, and what it said */
    int look_gameplay;
    int seen_look;
    int previous_gameplay;
    int previous_escape;
    unsigned int previous_buttons;
    unsigned int swallowed;      /* mouse buttons held across a change of mode, ignored until released */
    int wheel;                   /* wheel movement since the last read: +1 up, -1 down */
    uint32_t tap_until[16];      /* a short press is held this long, so the game's frame sees it */
    uint32_t wheel_until[2];
    uint32_t indicator_time;     /* when the capture last changed (0: never) */
    int indicator_captured;
} pc;

void port_controls_init(const char *layout) {
    memset(&pc, 0, sizeof(pc));
    pc.focused = 1;
    if (layout != NULL && strcmp(layout, "verdite") == 0) {
        pc.enabled = 1;
    } else if (layout != NULL && strcmp(layout, "ps2") != 0) {
        PORT_FATAL("--controls: \"%s\" is not a layout (ps2, verdite)", layout);
    }
}

int port_controls_verdite(void) {
    return pc.enabled && port_options.input == NULL;
}

/* The player was in control at the game's last frame */
static int in_gameplay(void) {
    return pc.seen_look && pc.look_gameplay && port_vblank_count() - pc.look_vblank <= 2;
}

int port_mouse_look(int gameplay, float *yaw, float *pitch) {
    double y;
    double p;

    *yaw = *pitch = 0.0f;
    if (!port_controls_verdite() || port_options.record_input != NULL) {
        return 0;
    }
    pc.seen_look = 1;
    pc.look_vblank = port_vblank_count();
    pc.look_gameplay = gameplay;
    if (!gameplay || !pc.captured) {
        pc.yaw = pc.pitch = 0.0;
        return 0;
    }
    y = pc.yaw < -MAX_TURN ? -MAX_TURN : pc.yaw > MAX_TURN ? MAX_TURN : pc.yaw;
    p = pc.pitch < -MAX_TURN ? -MAX_TURN : pc.pitch > MAX_TURN ? MAX_TURN : pc.pitch;
    pc.yaw = pc.pitch = 0.0;
    if (y == 0.0 && p == 0.0) {
        return 0;
    }
    *yaw = (float)y;
    *pitch = (float)p;
    return 1;
}

#ifdef KF4_HAVE_SDL
/* libpad's button bits */
#define B_SELECT 0x0001u
#define B_START 0x0008u
#define B_UP 0x0010u
#define B_RIGHT 0x0020u
#define B_DOWN 0x0040u
#define B_LEFT 0x0080u
#define B_L2 0x0100u
#define B_R2 0x0200u
#define B_L1 0x0400u
#define B_R1 0x0800u
#define B_TRIANGLE 0x1000u
#define B_CIRCLE 0x2000u
#define B_CROSS 0x4000u
#define B_SQUARE 0x8000u
#define TAP_BUTTONS (B_SELECT | B_START | B_TRIANGLE | B_CIRCLE | B_CROSS | B_SQUARE)
#define TAP_MS 100u

static void set_captured(int captured) {
    if (captured == pc.captured) {
        return;
    }
    pc.captured = captured;
    pc.yaw = pc.pitch = 0.0;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    SDL_GetRelativeMouseState(NULL, NULL); /* motion before the change is dropped */
    pc.indicator_time = SDL_GetTicks() | 1u;
    pc.indicator_captured = captured;
}

void port_controls_event(const void *sdl_event) {
    const SDL_Event *event = sdl_event;

    if (!port_controls_verdite()) {
        return;
    }
    if (event->type == SDL_MOUSEWHEEL) {
        pc.wheel += event->wheel.y > 0 ? 1 : event->wheel.y < 0 ? -1 : 0;
    } else if (event->type == SDL_WINDOWEVENT) {
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            pc.focused = 0;
            set_captured(0);
        } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            pc.focused = 1;
        }
    }
}

/* Once a vblank: the keyboard and mouse into libpad's buttons */
void port_controls_read(unsigned int *buttons) {
    static const struct {
        SDL_Scancode key;
        unsigned int bit;
    } map[] = {
        {SDL_SCANCODE_W, B_UP},      {SDL_SCANCODE_UP, B_UP},        {SDL_SCANCODE_S, B_DOWN},
        {SDL_SCANCODE_DOWN, B_DOWN}, {SDL_SCANCODE_LEFT, B_LEFT},    {SDL_SCANCODE_RIGHT, B_RIGHT},
        {SDL_SCANCODE_RETURN, B_START}, {SDL_SCANCODE_A, B_L1},      {SDL_SCANCODE_D, B_R1},
        {SDL_SCANCODE_SPACE, B_SQUARE}, {SDL_SCANCODE_F, B_CROSS},   {SDL_SCANCODE_Q, B_TRIANGLE},
        {SDL_SCANCODE_TAB, B_CIRCLE}, {SDL_SCANCODE_RSHIFT, B_SELECT},
    };
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    uint32_t now = SDL_GetTicks();
    unsigned int pressed = 0;
    unsigned int mouse = 0;
    Uint32 state;
    int dx = 0;
    int dy = 0;
    int gameplay = in_gameplay();
    int escape;
    size_t i;
    int bit;

    if (pc.focused) {
        for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
            if (keys[map[i].key]) {
                pressed |= map[i].bit;
            }
        }
    }

    /* Escape toggles the capture; one held through a change of focus does not */
    escape = pc.focused && keys[SDL_SCANCODE_ESCAPE];
    if (escape && !pc.previous_escape) {
        set_captured(!pc.captured);
    }
    pc.previous_escape = escape;

    state = SDL_GetRelativeMouseState(&dx, &dy);
    if (pc.focused) {
        mouse = (state & SDL_BUTTON_LMASK ? 1u : 0u) | (state & SDL_BUTTON_RMASK ? 2u : 0u) |
                (state & SDL_BUTTON_MMASK ? 4u : 0u);
    }
    /* a button held when the mode changes does nothing until it is let go: a menu's confirmation does
       not become an attack */
    if (gameplay != pc.previous_gameplay) {
        pc.swallowed = mouse;
    }
    pc.swallowed &= mouse;
    mouse &= ~pc.swallowed;
    if (gameplay) {
        if (pc.captured) {
            pressed |= (mouse & 1u ? B_SQUARE : 0u) | (mouse & 2u ? B_TRIANGLE : 0u) | (mouse & 4u ? B_CROSS : 0u);
            pc.yaw += dx * RADIANS_PER_PIXEL;
            pc.pitch -= dy * RADIANS_PER_PIXEL;
        }
    } else {
        pressed |= (mouse & 1u ? B_CROSS : 0u) | (mouse & 2u ? B_CIRCLE : 0u) | (mouse & 4u ? B_CROSS : 0u);
        if (pc.wheel > 0) {
            pc.wheel_until[0] = now + TAP_MS;
        } else if (pc.wheel < 0) {
            pc.wheel_until[1] = now + TAP_MS;
        }
        pc.yaw = pc.pitch = 0.0;
    }
    pc.wheel = 0;
    if ((int32_t)(pc.wheel_until[0] - now) > 0) {
        pressed |= B_UP;
    }
    if ((int32_t)(pc.wheel_until[1] - now) > 0) {
        pressed |= B_DOWN;
    }

    /* a tap is held 100 ms, so a frame of the game's (two vblanks or more) sees it; the movement
       keys are not, so letting go of them stops at once */
    for (bit = 0; bit < 16; bit++) {
        unsigned int mask = 1u << bit;

        if ((TAP_BUTTONS & mask) && (pressed & mask) && !(pc.previous_buttons & mask)) {
            pc.tap_until[bit] = now + TAP_MS;
        }
        if (pc.focused && (int32_t)(pc.tap_until[bit] - now) > 0) {
            *buttons |= mask;
        }
    }
    pc.previous_buttons = pressed;
    pc.previous_gameplay = gameplay;
    *buttons |= pressed;
}

/* The mouse in the window's top right corner when the capture changes: fades in over 140 ms, stays
   1100 ms, fades out over 420 ms. A cut across it means released. The glyph and timing are Verdite2's
   (patches/MouseIndicator.cs, MIT, Voicedrew11). */
void port_controls_draw(void *sdl_renderer, int x, int y, int width, int height) {
    static const char *const glyph[13] = {
        "....#....", "....#....", "...###...", "..#####..", ".##.#.##.", ".##.#.##.", ".#######.",
        ".#######.", ".#######.", ".#######.", ".#######.", "..#####..", "...###...",
    };
    SDL_Renderer *renderer = sdl_renderer;
    uint32_t age;
    double alpha;
    int cell;
    int margin;
    int origin_x;
    int origin_y;
    int pass;
    Uint8 old_r, old_g, old_b, old_a;
    SDL_BlendMode old_blend;

    if (!port_controls_verdite() || pc.indicator_time == 0 || width < 64 || height < 64) {
        return;
    }
    age = SDL_GetTicks() - pc.indicator_time;
    if (age >= 1660) {
        return;
    }
    alpha = age < 140 ? age / 140.0 : age < 1240 ? 1.0 : (1660 - age) / 420.0;
    cell = height / 180 > 2 ? height / 180 : 2;
    margin = height * 3 / 100 > cell * 2 ? height * 3 / 100 : cell * 2;
    origin_x = x + width - margin - cell * 9;
    origin_y = y + margin;
    /* the window clears to the draw colour: it is put back as it was */
    SDL_GetRenderDrawColor(renderer, &old_r, &old_g, &old_b, &old_a);
    SDL_GetRenderDrawBlendMode(renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (pass = 0; pass < 2; pass++) {
        int offset = pass == 0 ? 1 : 0;
        int row;

        if (pass == 0) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)(255.0 * 0.55 * alpha));
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(255.0 * alpha));
        }
        for (row = 0; row < 13; row++) {
            int column;

            for (column = 0; column < 9; column++) {
                SDL_Rect rect;

                if (glyph[row][column] != '#') {
                    continue;
                }
                if (!pc.indicator_captured && (row - column == 4 || row - column == 5)) {
                    continue;
                }
                rect.x = origin_x + (column + offset) * cell;
                rect.y = origin_y + (row + offset) * cell;
                rect.w = cell;
                rect.h = cell;
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
    SDL_SetRenderDrawColor(renderer, old_r, old_g, old_b, old_a);
    SDL_SetRenderDrawBlendMode(renderer, old_blend);
}
#endif
