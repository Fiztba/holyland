/* --frame-rate 60: a picture at every vblank in play, where the game draws one every second vblank
   (port_frame_interp(), port/include/port/host.h).

   What the game does. In play, GameLoop_Run's loop runs once every two vblanks (30 frames a second):
   Display_SwapBuffers waits for the odd field's vblank (sceGsSyncV until it returns 1), shows the
   buffer drawn last time and sets the other to be drawn (libgraph's draw environment, by PATH3, with
   its clear of colour and Z), then Packet_SendChain sends the packet chain the previous pass built,
   which draws the whole frame into it; then the game moves everything on by one frame (input, the
   player, the camera, the entities) and builds the next chain. The display registers a vblank scans
   out are those it found, before the game's swap after it: the chain drawn after vblank 2k - 1 is
   shown at 2k + 2 and 2k + 3, and the one shown at 2k and 2k + 1 was drawn after 2k - 3. A frame's
   camera is that of the pass before the one that sends it. (Pictures dumped in play change at even
   vblanks only.) The menus, the title and the movies have loops of their own and no world.

   What is done here. The GIF packets that reach the GS between two vblanks are kept (the clear and
   the whole chain). When, at vblank 2k, they drew the game's world into the buffer not shown, they are
   kept on, and at vblank 2k + 1, when nothing else has been sent since and the same buffer is shown,
   they are sent to the GS again, drawn into the buffer shown, with every vertex of the 3D world moved
   to a camera half way between the frame shown and the one drawn: that is the picture of vblank
   2k + 1. The frame drawn is shown at 2k + 2 as before, so nothing is shown later than on the PS2
   (no more input lag): the picture between shows the camera's move half a game frame early.
   - Where the world is, and its cameras, the game says in the chain (game/frame_interp.c): a mark
     after each GameLoop_SetCameraMatrices of GameLoop_DrawFrame, with world_view_matrix and
     view_screen_matrix, and one after the world. Within the world (projection 0, modes 0 and 1: the
     world and its mirror image), the vertices VU1's programs 0x000, 0x280, 0x5A0 and 0x700 send
     (not 0x276's GIF packets: the 2D) in triangles are moved: from each one's screen position and its
     depth (1/Q, or the Z) back to the camera's space, by the change of camera, and projected again,
     with S, T and Q for the new depth. A vertex the new camera puts behind it, or past the guard band,
     drops its triangles (ADC), as the VU1 programs do. Models drawn at a fixed place before the eye
     (mode 2), the arm, the HUD, the screen passes and the 2D stay as they are.
   - The camera half way: the rotation by quaternion slerp, the position half way along the line. A
     cut (a turn of more than INTERP_MAX_TURN, a move of more than INTERP_MAX_MOVE) is not
     interpolated: the vblank shows the frame before, as on the PS2.
   - Nothing of the replay is left behind. What it draws into the frame goes into the shown buffer,
     which the next frame's clear wipes (FRAME, and TEX0 and BITBLTBUF where they read the frame, point
     there instead). Everything else it draws (the scrolling textures' 128x128 targets, the
     reflection's 256x256 view) is drawn again from the same packets, so into the same pixels, and the
     uploads are sent again in their order, so every draw finds the textures it found the first time
     and VRAM ends as it was. Local-to-local transfers into anything but the frame (the copy of the
     frame the screen wobble and the map change read) and local-to-host ones are not started; FINISH,
     SIGNAL and LABEL are not sent. The registers the replay changed (FRAME, TEX0, BITBLTBUF) are
     written back with what the game last wrote (TEX0 without a CLUT load). So the game's own frames
     are the same pixels with the switch on or off (checked: --dump-frames of the even vblanks).
     (Dropping the draws into the other targets instead does not work: on map 000 the sky samples a
     128x128 target at FBP 0x1D4 that the frame draws, then draws over with another at 0x1D2.)
   - Not interpolated: what moves by itself (enemies, effects, animations: they move at 30 Hz, the
     camera at 60), the reflection and the copies of the frame (the picture between uses the frame's
     own), the previous frame blended in (in the picture between, the buffer it reads is the one being
     drawn, so nothing is blended in), 2D the world draws as sprites.
   With the switch off nothing here runs: the game sends no marks, and nothing is kept.

   Only the DMA controller's thread runs here (port/platform/gs.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"

#define INTERP_MAX_TURN 0.8   /* radians between two frames: more is a cut */
#define INTERP_MAX_MOVE 400.0 /* world units between two frames: more is a cut */

#define GS_PRIM 0x00
#define GS_XYZF2 0x04
#define GS_XYZ2 0x05
#define GS_TEX0_1 0x06
#define GS_TEX0_2 0x07
#define GS_XYZF3 0x0C
#define GS_XYZ3 0x0D
#define GS_PRMODECONT 0x1A
#define GS_PRMODE 0x1B
#define GS_FRAME_1 0x4C
#define GS_FRAME_2 0x4D
#define GS_BITBLTBUF 0x50
#define GS_TRXDIR 0x53
#define GS_SIGNAL 0x60
#define GS_FINISH 0x61
#define GS_LABEL 0x62

int port_frame_interp(void) {
    return port_options.frame_rate > 30;
}

/* ---- The GIF packets of the last vblank --------------------------------------------------------
   Each as a record: its path (bits 0-7), 1 << 8 when VU1's 3D programs sent it, its size. */
typedef struct {
    uint32_t what;
    uint32_t bytes;
} Record;
#define RECORD_3D 0x100

static uint8_t *kept;
static size_t kept_size, kept_room;
static int kept_whole; /* the paths were between packets when keeping began */

void port_interp_reset(int whole) {
    kept_size = 0;
    kept_whole = whole;
}

void port_interp_keep(int path, const void *data, size_t bytes) {
    Record r;

    if (!kept_whole) {
        return;
    }
    if (kept_size + sizeof(r) + bytes > kept_room) {
        size_t room = (kept_size + sizeof(r) + bytes) * 2;
        uint8_t *grown = realloc(kept, room);

        if (grown == NULL) {
            kept_whole = 0;
            return;
        }
        kept = grown;
        kept_room = room;
    }
    r.what = (uint32_t)path;
    if (path == 1 && !(port_vu1_run_pc >= 0x276 && port_vu1_run_pc < 0x280)) {
        r.what |= RECORD_3D; /* not program 0x276, which passes the game's own GIF packets on */
    }
    r.bytes = (uint32_t)bytes;
    memcpy(kept + kept_size, &r, sizeof(r));
    memcpy(kept + kept_size + sizeof(r), data, bytes);
    kept_size += sizeof(r) + bytes;
}

/* ---- Cameras ------------------------------------------------------------------------------------ */
typedef struct {
    double r[3][3]; /* world to view: rotation (row, column) */
    double t[3];    /* and translation */
    double eye[3];  /* the camera's place in the world */
} Camera;

typedef struct {
    double sx, sy, cx, cy, cz, az; /* view to screen: X = sx x / w + cx, Y = sy y / w + cy, Z = cz + az / w */
} Screen;

static void camera_from_fields(Camera *c, const float *view) {
    int row, col;

    /* the fields are the columns' x, y and z: rotation columns 0-2, then the translation */
    for (col = 0; col < 3; col++) {
        for (row = 0; row < 3; row++) {
            c->r[row][col] = view[col * 3 + row];
        }
    }
    for (row = 0; row < 3; row++) {
        c->t[row] = view[9 + row];
    }
    for (col = 0; col < 3; col++) {
        c->eye[col] = -(c->r[0][col] * c->t[0] + c->r[1][col] * c->t[1] + c->r[2][col] * c->t[2]);
    }
}

static void quat_from(const double m[3][3], double q[4]) {
    double tr = m[0][0] + m[1][1] + m[2][2];

    if (tr > 0) {
        double s = sqrt(tr + 1.0) * 2;
        q[3] = 0.25 * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        double s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25 * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        double s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25 * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        double s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25 * s;
    }
}

static void matrix_from(const double q[4], double m[3][3]) {
    double x = q[0], y = q[1], z = q[2], w = q[3];

    m[0][0] = 1 - 2 * (y * y + z * z);
    m[0][1] = 2 * (x * y - z * w);
    m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w);
    m[1][1] = 1 - 2 * (x * x + z * z);
    m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w);
    m[2][1] = 2 * (y * z + x * w);
    m[2][2] = 1 - 2 * (x * x + y * y);
}

/* The camera a fraction f of the way from a to b; 0 when the two are a cut apart */
static int camera_between(const Camera *a, const Camera *b, double f, Camera *out) {
    double qa[4], qb[4], q[4], dot = 0, angle, move = 0;
    int i, row;

    quat_from(a->r, qa);
    quat_from(b->r, qb);
    for (i = 0; i < 4; i++) {
        dot += qa[i] * qb[i];
    }
    if (dot < 0) {
        for (i = 0; i < 4; i++) {
            qb[i] = -qb[i];
        }
        dot = -dot;
    }
    dot = dot > 1 ? 1 : dot;
    angle = acos(dot); /* half the turn */
    for (i = 0; i < 3; i++) {
        move += (b->eye[i] - a->eye[i]) * (b->eye[i] - a->eye[i]);
    }
    if (2 * angle > INTERP_MAX_TURN || move > INTERP_MAX_MOVE * INTERP_MAX_MOVE) {
        return 0;
    }
    if (angle < 1e-6) {
        for (i = 0; i < 4; i++) {
            q[i] = qa[i] + (qb[i] - qa[i]) * f;
        }
    } else {
        double wa = sin((1 - f) * angle) / sin(angle), wb = sin(f * angle) / sin(angle);

        for (i = 0; i < 4; i++) {
            q[i] = qa[i] * wa + qb[i] * wb;
        }
    }
    {
        double n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

        for (i = 0; i < 4; i++) {
            q[i] /= n;
        }
    }
    matrix_from(q, out->r);
    for (i = 0; i < 3; i++) {
        out->eye[i] = a->eye[i] + (b->eye[i] - a->eye[i]) * f;
    }
    for (row = 0; row < 3; row++) {
        out->t[row] = -(out->r[row][0] * out->eye[0] + out->r[row][1] * out->eye[1] + out->r[row][2] * out->eye[2]);
    }
    return 1;
}

/* view space of camera `from` to view space of camera `to`: rotation and translation */
typedef struct {
    double r[3][3], t[3];
} Move;

static void move_between(const Camera *from, const Camera *to, Move *m) {
    int i, j, k;

    /* to.r * from.r^T (p - from.t) + to.t */
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            m->r[i][j] = 0;
            for (k = 0; k < 3; k++) {
                m->r[i][j] += to->r[i][k] * from->r[j][k];
            }
        }
    }
    for (i = 0; i < 3; i++) {
        m->t[i] = to->t[i] - (m->r[i][0] * from->t[0] + m->r[i][1] * from->t[1] + m->r[i][2] * from->t[2]);
    }
}

/* ---- Walking the packets -------------------------------------------------------------------------
   One walk reads the kept packets (SCAN), another sends a changed copy of them to the GS (REPLAY). */
enum { SCAN, REPLAY };
enum { SECTION_NONE, SECTION_WORLD, SECTION_OTHER };

typedef struct {
    uint32_t left, flg, nreg, reg, dwords;
    uint64_t regs;
    uint32_t prim;
    uint8_t *st;      /* PACKED: the ST qword before the next vertex, in this record */
    int adc_left;     /* vertices still to drop after one the new camera cannot see */
} GifWalk;

typedef struct {
    int mode;
    GifWalk paths[4];
    /* the marks */
    float fields[19];
    int section;
    int world_seen;   /* a world mark has been seen */
    Camera camera;    /* SCAN: the first world mark's camera */
    Screen screen;
    uint32_t draw_fbp; /* SCAN: FRAME_1's FBP at the first world mark: the buffer drawn */
    /* the GS registers as the game wrote them */
    uint64_t frame[2], tex0[2], bitbltbuf, prmodecont, prmode;
    int frame_set[2], tex0_set[2], bitbltbuf_set;
    /* REPLAY */
    uint32_t shown_fbp; /* the buffer the replay draws into, in place of draw_fbp */
    Move move;          /* the frame's view to the camera between */
    uint64_t moved, dropped; /* vertices moved, and dropped (ADC) */
} Walk;

/* A primitive draws into the frame: its context's FRAME is the buffer drawn (PRIM's CTXT, or PRMODE's
   when PRMODECONT says so). Only those vertices are moved: whatever else the world draws into (none
   seen so far) is drawn again as it was. */
static int draws_frame(const Walk *w, uint32_t prim) {
    int n = (w->prmodecont & 1) ? (int)((prim >> 9) & 1) : (int)((w->prmode >> 9) & 1);

    return w->frame_set[n] && (w->frame[n] & 0x1FF) == w->draw_fbp;
}

static void mark(Walk *w, uint64_t value) {
    uint32_t index = (uint32_t)(value >> 32) & 0xFF;
    uint32_t bits = (uint32_t)value;


    if (index >= PORT_FRAME_INTERP_VIEW && index < PORT_FRAME_INTERP_SCREEN + 6) {
        memcpy(&w->fields[index], &bits, 4);
        return;
    }
    if (index != PORT_FRAME_INTERP_HEADER) {
        return;
    }
    if (bits == PORT_FRAME_INTERP_END) {
        w->section = SECTION_NONE;
        return;
    }
    if ((bits >> 8) == 2 || (bits & 0xFF) != 0) {
        w->section = SECTION_OTHER; /* before the eye, or the 256x256 view of the reflection */
        return;
    }
    w->section = SECTION_WORLD;
    if (w->mode == SCAN && !w->world_seen) {
        const float *s = &w->fields[PORT_FRAME_INTERP_SCREEN];

        camera_from_fields(&w->camera, &w->fields[PORT_FRAME_INTERP_VIEW]);
        w->screen.sx = s[0];
        w->screen.sy = s[1];
        w->screen.cx = s[2];
        w->screen.cy = s[3];
        w->screen.cz = s[4];
        w->screen.az = s[5];
        w->draw_fbp = w->frame_set[0] ? (uint32_t)(w->frame[0] & 0x1FF) : 0xFFFFFFFFu;
    }
    w->world_seen = 1;
}

/* A vertex of the world (PACKED XYZF2 or XYZ2, q its qword) seen from the camera between */
static void move_vertex(Walk *w, GifWalk *p, uint8_t *q, int xyzf) {
    const Screen *s = &w->screen;
    uint64_t lo, hi;
    double X, Y, z, depth = 0, v[3], m[3], nx, ny, nz;
    int64_t ix, iy;
    int i, drop = 0;
    float sq[3] = {0, 0, 0};

    memcpy(&lo, q, 8);
    memcpy(&hi, q + 8, 8);
    X = (double)(lo & 0xFFFF) / 16.0;
    Y = (double)((lo >> 32) & 0xFFFF) / 16.0;
    if (p->st != NULL && (p->prim & 0x10) && !(p->prim & 0x100)) {
        memcpy(sq, p->st, 12);
        if (sq[2] > 0 && isfinite(sq[2])) {
            depth = 1.0 / sq[2];
        }
    }
    z = xyzf ? (double)((hi >> 4) & 0xFFFFFF) : (double)(uint32_t)hi;
    if (depth == 0 && z - s->cz > 0) {
        depth = s->az / (z - s->cz);
    }
    if (!(depth > 0) || !isfinite(depth)) {
        return;
    }
    v[0] = (X - s->cx) * depth / s->sx;
    v[1] = (Y - s->cy) * depth / s->sy;
    v[2] = depth;
    for (i = 0; i < 3; i++) {
        m[i] = w->move.r[i][0] * v[0] + w->move.r[i][1] * v[1] + w->move.r[i][2] * v[2] + w->move.t[i];
    }
    if (!(m[2] > 0)) {
        drop = 1;
    } else {
        /* clipped as the VU1 programs clip: past the guard band, or Z below 0 (beyond the far plane) */
        nx = s->sx * m[0] / m[2] + s->cx;
        ny = s->sy * m[1] / m[2] + s->cy;
        /* Z moves by what the projection's Z moves by: models drawn with a Z of their own (the
           flames, pulled in front) keep their offset */
        nz = z + s->az / m[2] - s->az / depth;
        if (!(fabs(nx - 2048.0) <= 2047.0 && fabs(ny - 2048.0) <= 2047.0 && nz >= 0)) {
            drop = 1;
        } else {
            ix = (int64_t)floor(nx * 16.0 + 0.5);
            iy = (int64_t)floor(ny * 16.0 + 0.5);
            nz = floor(nz + 0.5);
            lo = (lo & ~(uint64_t)0xFFFF & ~((uint64_t)0xFFFF << 32)) | (uint64_t)ix | ((uint64_t)iy << 32);
            if (xyzf) {
                uint64_t z = nz < 0 ? 0 : nz > 16777215.0 ? 16777215u : (uint64_t)nz;

                hi = (hi & ~((uint64_t)0xFFFFFF << 4)) | (z << 4);
            } else {
                uint64_t z = nz < 0 ? 0 : nz > 4294967295.0 ? 4294967295u : (uint64_t)nz;

                hi = (hi & ~(uint64_t)0xFFFFFFFF) | z;
            }
            if (p->st != NULL && (p->prim & 0x10) && !(p->prim & 0x100) && sq[2] > 0) {
                double f = depth / m[2];

                sq[0] = (float)(sq[0] * f);
                sq[1] = (float)(sq[1] * f);
                sq[2] = (float)(1.0 / m[2]);
                memcpy(p->st, sq, 12);
            }
            w->moved++;
        }
    }
    if (drop) {
        if (!((hi >> 47) & 1)) {
            w->dropped++; /* (a vertex the game already dropped is not counted) */
        }
        p->adc_left = 3; /* this vertex and the two after it: every triangle with it */
    }
    if (p->adc_left > 0) {
        p->adc_left--;
        hi |= (uint64_t)1 << 47;
    }
    memcpy(q, &lo, 8);
    memcpy(q + 8, &hi, 8);
}

/* A+D, or a PACKED register with its own descriptor (TEX0): what the game wrote, and in REPLAY the
   copy changed. reg may be changed (to a register that does nothing, or one that does not draw). */
static void ad_write(Walk *w, GifWalk *p, uint32_t *reg, uint64_t *value) {
    uint32_t r = *reg;


    switch (r) {
    case GS_PRIM:
        p->prim = (uint32_t)(*value & 0x7FF);
        break;
    case GS_TEX0_1:
    case GS_TEX0_2: {
        int n = (int)(r - GS_TEX0_1);

        w->tex0[n] = *value;
        w->tex0_set[n] = 1;
        if (w->mode == REPLAY && (*value & 0x3FFF) == w->draw_fbp * 32) {
            *value = (*value & ~(uint64_t)0x3FFF) | (w->shown_fbp * 32);
        }
        break;
    }
    case GS_PRMODECONT:
        w->prmodecont = *value;
        break;
    case GS_PRMODE:
        w->prmode = *value;
        break;
    case GS_FRAME_1:
    case GS_FRAME_2: {
        int n = (int)(r - GS_FRAME_1);

        w->frame[n] = *value;
        w->frame_set[n] = 1;
        if (w->mode == REPLAY) {
            if ((*value & 0x1FF) == w->draw_fbp) {
                *value = (*value & ~(uint64_t)0x1FF) | w->shown_fbp;
            }
        }
        break;
    }
    case GS_BITBLTBUF:
        w->bitbltbuf = *value;
        w->bitbltbuf_set = 1;
        if (w->mode == REPLAY) {
            if ((*value & 0x3FFF) == w->draw_fbp * 32) {
                *value = (*value & ~(uint64_t)0x3FFF) | (w->shown_fbp * 32);
            }
            if (((*value >> 32) & 0x3FFF) == w->draw_fbp * 32) {
                *value = (*value & ~((uint64_t)0x3FFF << 32)) | ((uint64_t)(w->shown_fbp * 32) << 32);
            }
        }
        break;
    case GS_TRXDIR:
        if (w->mode == REPLAY) {
            uint32_t dbp = (uint32_t)(w->bitbltbuf >> 32) & 0x3FFF;
            uint32_t dir = (uint32_t)(*value & 3);

            /* (bitbltbuf is the game's: a transfer into the buffer drawn goes into the one shown) */
            if (dir == 1 || (dir == 2 && dbp != w->draw_fbp * 32)) {
                *reg = PORT_GS_REG_FRAME_INTERP;
                *value = (uint64_t)0xFF << 32;
            }
        }
        break;
    case GS_SIGNAL:
    case GS_FINISH:
    case GS_LABEL:
        if (w->mode == REPLAY) {
            *reg = PORT_GS_REG_FRAME_INTERP;
            *value = (uint64_t)0xFF << 32;
        }
        break;
    case PORT_GS_REG_FRAME_INTERP:
        mark(w, *value);
        break;
    }
}

/* One record: in REPLAY, data is the copy to send, and is changed */
static void walk_record(Walk *w, int path, int is_3d, uint8_t *data, size_t bytes) {
    GifWalk *p = &w->paths[path];
    size_t count = bytes / 16, i = 0;

    p->st = NULL;
    while (i < count) {
        uint8_t *q = data + i * 16;

        if (p->left == 0) {
            uint64_t tag, regs;
            uint32_t nloop, n;

            memcpy(&tag, q, 8);
            memcpy(&regs, q + 8, 8);
            nloop = (uint32_t)(tag & 0x7FFF);
            p->flg = (uint32_t)((tag >> 58) & 3);
            p->nreg = (uint32_t)(tag >> 60);
            if (p->nreg == 0) {
                p->nreg = 16;
            }
            p->regs = regs;
            p->reg = 0;
            if ((tag >> 46) & 1) {
                p->prim = (uint32_t)(tag >> 47) & 0x7FF; /* PRE */
            }
            if (p->flg == 0) {
                p->left = nloop * p->nreg;
            } else if (p->flg == 1) {
                p->left = (nloop * p->nreg + 1) / 2;
                p->dwords = nloop * p->nreg;
            } else {
                p->left = nloop;
            }
            i++;
            continue;
        }
        if (p->flg == 0) {
            uint32_t d = (uint32_t)(p->regs >> (p->reg * 4)) & 0xF;
            uint64_t v[2];

            memcpy(v, q, 16);
            switch (d) {
            case 0x0:
                p->prim = (uint32_t)(v[0] & 0x7FF);
                break;
            case 0x2:
                p->st = q;
                break;
            case 0x4:
            case 0x5:
                if (w->mode == REPLAY && is_3d && w->section == SECTION_WORLD && (p->prim & 7) >= 3 &&
                    (p->prim & 7) <= 5 && draws_frame(w, p->prim)) {
                    move_vertex(w, p, q, d == 0x4);
                }
                p->st = NULL;
                break;
            case 0x6:
            case 0x7: {
                uint32_t reg = d;

                ad_write(w, p, &reg, &v[0]);
                memcpy(q, &v[0], 8);
                break;
            }
            case 0xE: {
                uint32_t reg = (uint32_t)(v[1] & 0xFF);

                ad_write(w, p, &reg, &v[0]);
                v[1] = (v[1] & ~(uint64_t)0xFF) | reg;
                memcpy(q, v, 16);
                break;
            }
            }
            p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            p->left--;
            i++;
        } else if (p->flg == 1) {
            int half;

            for (half = 0; half < 2 && p->dwords != 0; half++, p->dwords--) {
                uint32_t d = (uint32_t)(p->regs >> (p->reg * 4)) & 0xF;
                uint64_t v;

                memcpy(&v, q + half * 8, 8);
                if (d == 0x0 || d == 0x6 || d == 0x7) {
                    uint32_t reg = d;

                    ad_write(w, p, &reg, &v);
                    memcpy(q + half * 8, &v, 8);
                }
                p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            }
            p->left--;
            i++;
        } else {
            size_t n = count - i < p->left ? count - i : p->left;

            p->left -= (uint32_t)n;
            i += n;
        }
    }
}

/* ---- At a vblank ----------------------------------------------------------------------------
   The display registers of vblank n are those the vblank found, before the game swapped at it: the
   chain drawn after the game's swap at vblank 2k - 1 is kept by vblank 2k (where the frame it swapped
   to is first shown), and drawn again for vblank 2k + 1, when nothing else has been sent since and the
   same buffer is still shown. */
typedef struct {
    uint8_t *data;
    size_t size;
    Walk scan;        /* what the walk found in it */
    uint32_t shown;   /* the buffer shown at the vblank that kept it */
    Camera before;    /* the camera of the frame shown then */
    int have_before;
} Pending;

static Pending pending; /* a frame drawn and not yet shown (size 0: none) */
static size_t pending_room;
static Camera last_camera; /* the camera of the last frame drawn */
static int last_camera_set;
static uint8_t *copy;
static size_t copy_room;
static uint64_t stat_replays, stat_cuts, stat_moved, stat_dropped;

void port_interp_forget(void) {
    last_camera_set = 0;
    pending.size = 0;
    pending.have_before = 0;
    port_interp_reset(0);
}

/* KF4_INTERP_STILL=1, a check: the picture between is drawn with the camera of the frame drawn, so it
   must be that frame (the next vblank's picture) */
static int interp_still(void) {
    static int still = -1;

    if (still < 0) {
        still = getenv("KF4_INTERP_STILL") != NULL;
    }
    return still;
}

static void walk_all(Walk *w, uint8_t *data, size_t size, int mode) {
    size_t at;

    memset(w, 0, sizeof(*w));
    w->mode = mode;
    w->prmodecont = 1;
    for (at = 0; at < size;) {
        Record r;

        memcpy(&r, data + at, sizeof(r));
        walk_record(w, (int)(r.what & 0xFF), (r.what & RECORD_3D) != 0, data + at + sizeof(r), r.bytes);
        at += sizeof(r) + r.bytes;
    }
}

/* The pending frame again, into the buffer shown, seen from the camera between */
static void replay_pending(const Camera *between, void (*send)(int path, const void *data, size_t bytes)) {
    static Walk w; /* (large: kept off the stack) */
    const Walk *scan = &pending.scan;
    uint64_t fix[2 * 8];
    size_t at;
    int n = 0, i;

    memset(&w, 0, sizeof(w));
    w.mode = REPLAY;
    w.prmodecont = 1;
    w.draw_fbp = scan->draw_fbp;
    w.shown_fbp = pending.shown;
    w.screen = scan->screen;
    move_between(&scan->camera, interp_still() ? &scan->camera : between, &w.move);
    for (at = 0; at < pending.size;) {
        Record r;

        memcpy(&r, pending.data + at, sizeof(r));
        if (copy_room < r.bytes) {
            free(copy);
            copy_room = r.bytes * 2;
            copy = malloc(copy_room);
            if (copy == NULL) {
                PORT_FATAL("out of memory");
            }
        }
        memcpy(copy, pending.data + at + sizeof(r), r.bytes);
        walk_record(&w, (int)(r.what & 0xFF), (r.what & RECORD_3D) != 0, copy, r.bytes);
        send((int)(r.what & 0xFF), copy, r.bytes);
        at += sizeof(r) + r.bytes;
    }
    /* the registers the replay changed, back to what the game left in them (by PATH3: every path is
       between packets) */
    for (i = 0; i < 2; i++) {
        if (scan->frame_set[i]) {
            n++;
            fix[2 * n] = scan->frame[i];
            fix[2 * n + 1] = (uint64_t)(GS_FRAME_1 + i);
        }
        if (scan->tex0_set[i]) {
            n++;
            fix[2 * n] = scan->tex0[i] & ~((uint64_t)7 << 61); /* CLD 0: no CLUT load */
            fix[2 * n + 1] = (uint64_t)(GS_TEX0_1 + i);
        }
    }
    if (scan->bitbltbuf_set) {
        n++;
        fix[2 * n] = scan->bitbltbuf;
        fix[2 * n + 1] = GS_BITBLTBUF;
    }
    if (n > 0) {
        fix[0] = (uint64_t)n | ((uint64_t)1 << 15) | ((uint64_t)1 << 60); /* NLOOP n, EOP, PACKED, NREG 1 */
        fix[1] = 0xE;                                                       /* A+D */
        send(3, fix, (size_t)(n + 1) * 16);
    }
    stat_replays++;
    stat_moved += w.moved;
    stat_dropped += w.dropped;
}

int port_interp_vsync(const uint64_t *privileged, void (*send)(int path, const void *data, size_t bytes)) {
    static Walk w; /* (large: kept off the stack) */
    /* the buffer shown: DISPFB2's, or DISPFB1's when only read circuit 1 is on */
    uint64_t dispfb = (privileged[0] & 2) ? privileged[0x90 / 8] : privileged[0x70 / 8];
    uint32_t shown = (uint32_t)(dispfb & 0x1FF);
    int done = 0;

    if (pending.size != 0) {
        Camera between;

        /* the vblank after the one that kept it: nothing sent since, the same buffer shown */
        if (kept_size == 0 && kept_whole && shown == pending.shown && pending.have_before) {
            if (camera_between(&pending.before, &pending.scan.camera, 0.5, &between)) {
                replay_pending(&between, send);
                done = 1;
            } else {
                stat_cuts++;
            }
        }
        pending.size = 0;
    }
    if (!kept_whole || kept_size == 0) {
        return done;
    }
    walk_all(&w, kept, kept_size, SCAN);
    if (!w.world_seen) {
        return done;
    }
    if (w.draw_fbp != 0xFFFFFFFFu && w.draw_fbp != shown && w.screen.sx != 0 && w.screen.sy != 0) {
        /* kept for the next vblank: the buffers swap, so the packets' becomes the pending one's */
        uint8_t *data = pending.data;
        size_t room = pending_room;

        pending.data = kept;
        pending_room = kept_room;
        pending.size = kept_size;
        kept = data;
        kept_room = room;
        kept_size = 0;
        pending.scan = w;
        pending.shown = shown;
        pending.before = last_camera;
        pending.have_before = last_camera_set;
    }
    last_camera = w.camera;
    last_camera_set = 1;
    return done;
}

void port_interp_print_stats(void) {
    fprintf(stderr, "[stats] frame rate 60: %llu pictures between drawn, %llu cuts not; %llu vertices moved, %llu dropped\n",
            (unsigned long long)stat_replays, (unsigned long long)stat_cuts, (unsigned long long)stat_moved,
            (unsigned long long)stat_dropped);
}
