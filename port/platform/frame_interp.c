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

/* ---- How many pictures, and when -------------------------------------------------------------
   The game's frames come every second vblank, at 59.94 vblanks a second. At F pictures a second there
   are F / 59.94 a vblank: when that is a whole number n (to 1%: 60, 120, 240, 360 Hz), the vblank's own
   picture and n - 1 more between it and the next, evenly; otherwise (144, 165 Hz) a picture every
   1 / (F / 59.94) of a vblank from vblank 0 on, wherever it falls. Each is the time the window shows it
   at (port/platform/video.c), and what the picture shows is the game's world at that time. */
int port_frame_rate_hz(void) {
    int hz = port_options.frame_rate;

    if (hz == PORT_FRAME_RATE_MONITOR) {
        hz = port_video_refresh_rate();
        hz = hz > 0 ? hz : 60;
    }
    return hz < 30 ? 30 : hz > 1000 ? 1000 : hz;
}

static double pictures_a_vblank(void) {
    double p = port_frame_rate_hz() * 1001.0 / 60000.0;
    double n = floor(p + 0.5);

    return n >= 1 && fabs(p - n) <= 0.01 * n ? n : p;
}

int port_interp_schedule(uint64_t vblank, double *phases, int max) {
    double p = pictures_a_vblank(), i;
    int n = 0;

    if (port_frame_rate_hz() <= 30 || p < 1.0 || max < 1) {
        phases[0] = 0;
        return 1;
    }
    if (p == floor(p)) {
        for (n = 0; n < (int)p && n < max; n++) {
            phases[n] = n / p;
        }
        return n;
    }
    for (i = ceil((double)vblank * p - 1e-6); i < (double)(vblank + 1) * p - 1e-6 && n < max; i++) {
        double phase = i / p - (double)vblank;

        phases[n++] = phase < 1e-6 ? 0 : phase;
    }
    return n;
}

/* ---- The GIF packets of the last vblank --------------------------------------------------------
   Each as a record: its path (bits 0-7), 1 << 8 when VU1's 3D programs sent it, its size, and for
   those the run that sent it (port_vif1_run_serial) and what that run's batch was (port_vif1_run_key). */
typedef struct {
    uint32_t what;
    uint32_t bytes;
    uint32_t serial;
    uint32_t unused;
    uint64_t key;
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
    r.serial = port_vif1_run_serial;
    r.unused = 0;
    r.key = port_vif1_run_key;
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
/* WORLD: the world (projection 0, modes 0 and 1); EYE: models before the eye (mode 2), in the camera's
   space; ARM: the arm and the held item, in the world but moving only as they move */
/* REFLECT: the reflection's view of the world (projection 1, mode 1, into a 256x256 target before the
   world), by the same camera as the world's (its own projection to the target) */
enum { SECTION_NONE, SECTION_WORLD, SECTION_OTHER, SECTION_EYE, SECTION_ARM, SECTION_REFLECT, SECTIONS };

/* ---- What moves by itself ------------------------------------------------------------------------
   Each VU1 run of a 3D program draws one batch of one model part. A run is known again in the next
   frame by its batch (the hash of what VIF1 unpacked for it: the part's vertices as they are in the
   model, before any matrix) and, among the runs of the frame before with the same batch and as many
   vertices, as the one whose middle is nearest, it being the nearest to that one in turn (the map's
   pieces repeat: the same batch is drawn in many places, and one that came into view is not taken for
   the one beside it). Its vertices, in the order the GS gets them, are kept for each frame as places
   in the world (before the eye, in the camera's space).
   - A paired run that moved (a vertex of it by more than a 500th of its distance from the eye) is drawn
     half way between its two places, seen from the camera half way: the enemies, the arm and the held
     item, the effects and the moving objects move at 60 too.
   - One that did not move is moved with the camera alone, as before; so is one with no pair (a model
     that came or went, a batch the CPU made this frame) in the world. Before the eye and in the arm,
     a run with no pair is left as it is.
   - Not paired: runs more than INTERP_MAX_MOVE apart (two things that share a batch), and runs whose
     half-way place comes much nearer the eye than both ends (the arm's swing turns round the shoulder,
     past the eye, in one frame: the straight line between cuts the corner).
   - A vertex's depth comes from Q (1/w), or from Z when its Q is not 1/w; the sphere-mapped second pass
     of program 0x280 (Q 1.0) goes over the first pass's points, and takes their depth (the arm's Z is
     pulled near, so its Z gives the wrong depth).
   - Vertices with ADC may have been clipped in one of the frames (object_between). */
typedef struct {
    uint64_t key;
    uint32_t first;    /* its first vertex in pos */
    uint32_t count;    /* its vertices */
    int section;
    int placed;        /* it has a vertex with a place */
    double middle[3];  /* the mean of its vertices' places (those without ADC) */
    uint32_t prim;     /* PRIM at its first vertex */
    uint64_t tex;      /* and TEX0's texture and CLUT */
    int32_t pair;      /* the run of the frame before that is this one (objects_pair), -1: none */
} Run;

#define VERTEX_SURE 1  /* a vertex without ADC: never one the VU1 program clipped */
#define VERTEX_MAYBE 2 /* with ADC: maybe one the VU1 program clipped, its place past the guard band
                          wrapped round (object_between). It is kept out of its run's middle and of how
                          far the run moved. */

typedef struct {
    Run *runs;
    size_t nruns, runs_room;
    double (*pos)[3];
    uint8_t *valid;
    size_t npos, pos_room;
    uint32_t *by_key; /* the runs' numbers, in the order of their batches */
    size_t by_key_room;
} Objects;

static void objects_reset(Objects *o) {
    o->nruns = 0;
    o->npos = 0;
}

static void objects_add_run(Objects *o, uint64_t key, int section) {
    Run *r;

    if (o->nruns == o->runs_room) {
        o->runs_room = o->runs_room ? o->runs_room * 2 : 4096;
        o->runs = realloc(o->runs, o->runs_room * sizeof(*o->runs));
        if (o->runs == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    r = &o->runs[o->nruns++];
    r->key = key;
    r->first = (uint32_t)o->npos;
    r->count = 0;
    r->section = section;
    r->prim = 0;
    r->tex = 0;
    r->pair = -1;
}

static void objects_add_vertex(Objects *o, const double place[3], int valid) {
    if (o->npos == o->pos_room) {
        o->pos_room = o->pos_room ? o->pos_room * 2 : 65536;
        o->pos = realloc(o->pos, o->pos_room * sizeof(*o->pos));
        o->valid = realloc(o->valid, o->pos_room);
        if (o->pos == NULL || o->valid == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    memcpy(o->pos[o->npos], place, sizeof(o->pos[0]));
    o->valid[o->npos] = (uint8_t)valid;
    o->npos++;
    o->runs[o->nruns - 1].count++;
}

/* After the walk: each run's middle, and the runs in the order of their batches */
static const Objects *sorting;

static int by_key(const void *a, const void *b) {
    uint64_t x = sorting->runs[*(const uint32_t *)a].key, y = sorting->runs[*(const uint32_t *)b].key;

    return x < y ? -1 : x > y ? 1 : (int)(*(const uint32_t *)a - *(const uint32_t *)b);
}

static void objects_finish(Objects *o) {
    size_t i, k;

    for (i = 0; i < o->nruns; i++) {
        Run *r = &o->runs[i];
        double sum[3] = {0, 0, 0};
        uint32_t n = 0;

        for (k = r->first; k < r->first + r->count; k++) {
            if (o->valid[k] == VERTEX_SURE) {
                sum[0] += o->pos[k][0];
                sum[1] += o->pos[k][1];
                sum[2] += o->pos[k][2];
                n++;
            }
        }
        r->placed = n != 0;
        for (k = 0; k < 3; k++) {
            r->middle[k] = n ? sum[k] / n : 0;
        }
    }
    if (o->by_key_room < o->nruns) {
        o->by_key_room = o->nruns * 2;
        free(o->by_key);
        o->by_key = malloc(o->by_key_room * sizeof(*o->by_key));
        if (o->by_key == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    for (i = 0; i < o->nruns; i++) {
        o->by_key[i] = (uint32_t)i;
    }
    sorting = o;
    qsort(o->by_key, o->nruns, sizeof(*o->by_key), by_key);
}

/* The run of the frame before that is this one (cur, of this frame), or NULL */
static const Run *objects_find(const Objects *o, const Run *cur) {
    size_t lo = 0, hi, i;
    const Run *best = NULL;
    double best_d2 = INTERP_MAX_MOVE * INTERP_MAX_MOVE;

    if (o == NULL || !cur->placed) {
        return NULL;
    }
    hi = o->nruns;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (o->runs[o->by_key[mid]].key < cur->key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (i = lo; i < o->nruns && o->runs[o->by_key[i]].key == cur->key; i++) {
        const Run *r = &o->runs[o->by_key[i]];
        double d2 = 0;
        int k;

        if (r->count != cur->count || !r->placed || r->section != cur->section) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            d2 += (r->middle[k] - cur->middle[k]) * (r->middle[k] - cur->middle[k]);
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            best = r;
        }
    }
    return best;
}

/* ---- What the frame writes and reads ------------------------------------------------------------
   In the order of the packets, the VRAM pages each register write says will be read (a texture: twice
   its first level when it has mipmaps; a CLUT) or written (a draw's FRAME and ZBUF, to its scissor's
   bottom; an upload or a local copy, to the rows it reaches): worked out generously, from the first
   block. A 2D item drawn differently into a buffer the frame reads before it draws it needs the frame
   drawn again exact after the pictures between (flat_pair). */
enum { EV_UPLOAD, EV_READ, EV_WRITE };

typedef struct {
    uint32_t first, last; /* pages (8 KB) */
    uint32_t kind;
} Event;

typedef struct {
    Event *ev;
    size_t nev, ev_room;
    /* the copies of the frame drawn into another buffer (GsDraw_CopyFrame: the haze, the wobble, the
       screen's picture on a model), by their events */
    uint32_t *copies;
    size_t ncopies, copies_room;
} Uploads;

static void uploads_reset(Uploads *u) {
    u->nev = 0;
    u->ncopies = 0;
}

/* The pages a buffer covers: from block bp, bw * 64 pixels wide, rows rows of psm */
static void pages_of(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t rows, uint32_t *first, uint32_t *last) {
    uint32_t bits = psm == 0x02 || psm == 0x0A || psm == 0x32 || psm == 0x3A ? 16
                    : psm == 0x13                                             ? 8
                    : psm == 0x14                                             ? 4
                                                                              : 32;
    uint64_t bytes = (uint64_t)(bw ? bw : 1) * 64 * rows * bits / 8;

    *first = bp / 32;
    *last = (uint32_t)((bp * 256ull + (bytes ? bytes : 1) - 1) / 8192) + (bp % 32 ? 1 : 0);
}

static void uploads_event(Uploads *u, uint32_t kind, uint32_t first, uint32_t last) {
    Event *e;

    if (u == NULL) {
        return;
    }
    if (u->nev == u->ev_room) {
        u->ev_room = u->ev_room ? u->ev_room * 2 : 1024;
        u->ev = realloc(u->ev, u->ev_room * sizeof(*u->ev));
        if (u->ev == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    e = &u->ev[u->nev++];
    e->first = first;
    e->last = last;
    e->kind = kind;
}

/* ---- The 2D: what the CPU draws anew each frame ---------------------------------------------------
   Everything that is not a run of VU1's 3D programs (the HUD, the screen passes and fades, the
   scrolling textures' copies, effects the CPU makes) is sent by the game as GIF packets of its own.
   Each GIF tag with vertices is an item; a frame's items are paired with the frame before's in their
   order, by what they draw with (the primitive, the texture and CLUT, the buffer drawn into, the vertex
   count: the buffer drawn and the one shown, which swap each frame, by their part). A pair is taken
   when it is a likely one: no vertex more than 128 pixels from its place, and the texture coordinates
   the same, or all moved by one small step (a scrolling texture: within a quarter of the item's own
   width of texture, after the texture's wrap), or changed only where the vertices moved (a gauge that
   grows). A glyph changing in place, or anything else, is drawn as the frame has it. A picture between
   draws a paired item with its places, colours (fades) and texture coordinates where they are at that
   time. Images the CPU makes anew (the orb's swirl, a flipbook's next picture) cannot be: the texture
   itself changed. */
typedef struct {
    uint32_t prim;     /* PRIM (type and switches) */
    uint64_t tex;      /* TEX0's texture and CLUT */
    uint32_t fbp;      /* FRAME's buffer */
    uint32_t first;    /* its first vertex in verts */
    uint32_t count;
    int32_t pair;      /* the item of the frame before (-1: none, or nothing to do) */
    float wrap_s, wrap_t; /* added to the frame before's texture coordinates (a scroll past the wrap) */
} Item;

typedef struct {
    int32_t x, y;      /* (1/16 pixel) */
    uint8_t rgba[4];
    float s, t;        /* ST, or UV (in texels) */
    uint8_t has_rgba, coords; /* coords: 0 none, 1 ST, 2 UV */
} FlatVertex;

typedef struct {
    Item *items;
    size_t nitems, items_room;
    FlatVertex *verts;
    size_t nverts, verts_room;
    uint32_t draw_fbp, shown_fbp; /* the frame's buffers */
    int restore;                   /* a paired item draws into a buffer read before it is drawn in the frame */
    /* by copy of the frame (Uploads): made again in the pictures between, from their own frame. Only a
       copy the frame itself reads after it, and not before: then the next frame makes its own before it
       reads it (the haze, the wobble). One it does not read (the map change keeps its last frame there,
       for the frames after to fade out) is left as the game's frame made it. */
    uint8_t *copy_ok;
    size_t ncopies, copy_room;
} Flat;

static void flat_reset(Flat *f) {
    f->nitems = 0;
    f->nverts = 0;
    f->restore = 0;
}

static Item *flat_add_item(Flat *f, uint32_t prim, uint64_t tex0, uint32_t fbp) {
    Item *it;

    if (f->nitems == f->items_room) {
        f->items_room = f->items_room ? f->items_room * 2 : 1024;
        f->items = realloc(f->items, f->items_room * sizeof(*f->items));
        if (f->items == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    it = &f->items[f->nitems++];
    it->prim = prim;
    /* TBP0, TBW, PSM, TW, TH, TCC, TFX; CBP, CPSM, CSM, CSA (not CLD) */
    it->tex = (prim & 0x10) ? tex0 & ~((uint64_t)7 << 61) : 0;
    it->fbp = fbp;
    it->first = (uint32_t)f->nverts;
    it->count = 0;
    it->pair = -1;
    it->wrap_s = it->wrap_t = 0;
    return it;
}

static FlatVertex *flat_add_vertex(Flat *f) {
    if (f->nverts == f->verts_room) {
        f->verts_room = f->verts_room ? f->verts_room * 2 : 8192;
        f->verts = realloc(f->verts, f->verts_room * sizeof(*f->verts));
        if (f->verts == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    f->items[f->nitems - 1].count++;
    return &f->verts[f->nverts++];
}

/* What an item draws with, the swapping buffers by their part */
static int items_alike(const Flat *fa, const Item *a, const Flat *fb, const Item *b) {
    uint32_t pa = a->fbp == fa->draw_fbp ? 0x200 : a->fbp == fa->shown_fbp ? 0x201 : a->fbp;
    uint32_t pb = b->fbp == fb->draw_fbp ? 0x200 : b->fbp == fb->shown_fbp ? 0x201 : b->fbp;
    uint64_t ta = a->tex, tb = b->tex;
    uint32_t ba = (uint32_t)(ta & 0x3FFF), bb = (uint32_t)(tb & 0x3FFF);

    if (a->prim & 0x10) {
        ba = ba == fa->draw_fbp * 32 ? 0x4000 : ba == fa->shown_fbp * 32 ? 0x4001 : ba;
    }
    if (b->prim & 0x10) {
        bb = bb == fb->draw_fbp * 32 ? 0x4000 : bb == fb->shown_fbp * 32 ? 0x4001 : bb;
    }
    return a->prim == b->prim && a->count == b->count && pa == pb && ba == bb &&
           (ta & ~(uint64_t)0x3FFF) == (tb & ~(uint64_t)0x3FFF);
}

/* An item of this frame (b) and one of the frame before (a) are a likely pair, and something of it
   changed: 1, with the wrap to add to a's texture coordinates */
static int items_pair(const Flat *fa, const Item *a, const Flat *fb, const Item *b, float *wrap_s, float *wrap_t) {
    uint32_t k;
    int changed = 0, uniform = 1, moved_only = 1;
    float ds0 = 0, dt0 = 0, smin = 1e30f, smax = -1e30f, tmin = 1e30f, tmax = -1e30f, dsmost = 0, dtmost = 0;
    /* the texture's size: 1.0 in ST, its texels in UV */
    float tw = (float)(1u << ((b->tex >> 26) & 0xF)), th = (float)(1u << ((b->tex >> 30) & 0xF));

    *wrap_s = *wrap_t = 0;
    for (k = 0; k < b->count; k++) {
        const FlatVertex *va = &fa->verts[a->first + k], *vb = &fb->verts[b->first + k];
        int32_t dx = vb->x - va->x, dy = vb->y - va->y;

        if (dx > 2048 || dx < -2048 || dy > 2048 || dy < -2048) {
            return 0;
        }
        if (va->coords != vb->coords || va->has_rgba != vb->has_rgba) {
            return 0;
        }
        changed |= dx != 0 || dy != 0 || (vb->has_rgba && memcmp(va->rgba, vb->rgba, 4) != 0);
        if (vb->coords) {
            float ds = vb->s - va->s, dt = vb->t - va->t;
            float eps = vb->coords == 2 ? 0.07f : 1e-4f;

            if (k == 0) {
                ds0 = ds;
                dt0 = dt;
            } else if (fabsf(ds - ds0) > eps || fabsf(dt - dt0) > eps) {
                uniform = 0;
            }
            if ((fabsf(ds) > eps || fabsf(dt) > eps) && dx == 0 && dy == 0) {
                moved_only = 0;
            }
            dsmost = fabsf(ds) > dsmost ? fabsf(ds) : dsmost;
            dtmost = fabsf(dt) > dtmost ? fabsf(dt) : dtmost;
            smin = vb->s < smin ? vb->s : smin;
            smax = vb->s > smax ? vb->s : smax;
            tmin = vb->t < tmin ? vb->t : tmin;
            tmax = vb->t > tmax ? vb->t : tmax;
        }
    }
    if (b->count > 0 && fb->verts[b->first].coords) {
        float eps = fb->verts[b->first].coords == 2 ? 0.07f : 1e-4f;
        float size_s = fb->verts[b->first].coords == 2 ? tw : 1.0f, size_t_ = fb->verts[b->first].coords == 2 ? th : 1.0f;

        if (uniform && (fabsf(ds0) > eps || fabsf(dt0) > eps)) {
            /* all moved by one step: a scroll, the shortest way round the texture's wrap */
            float s = ds0 - size_s * floorf(ds0 / size_s + 0.5f), t = dt0 - size_t_ * floorf(dt0 / size_t_ + 0.5f);

            if (fabsf(s) > 0.25f * (smax - smin) + eps || fabsf(t) > 0.25f * (tmax - tmin) + eps) {
                return 0; /* (a glyph, or a picture, changed) */
            }
            *wrap_s = ds0 - s;
            *wrap_t = dt0 - t;
            changed = 1;
        } else if (!uniform) {
            /* changed only where the vertices moved (a gauge), or, not a sprite, each vertex's by a little
               of the item's width of texture (a flame's or the water's surface flowing); not a sprite's
               texture changed in place (a glyph) */
            if (!moved_only && ((b->prim & 7) == 6 || b->count < 3 || dsmost > 0.25f * (smax - smin) + eps ||
                                dtmost > 0.25f * (tmax - tmin) + eps)) {
                return 0;
            }
            changed = 1;
        }
    }
    return changed;
}

/* The frame reads the buffer at fbp before it draws into it, or uploads into it */
static int read_first(const Uploads *u, uint32_t fbp) {
    uint32_t first, last;
    size_t e;

    pages_of(fbp * 32, 1, 0, 1, &first, &last);
    for (e = 0; e < u->nev; e++) {
        const Event *ev = &u->ev[e];

        if (ev->kind == EV_READ && ev->first <= first && ev->last >= first) {
            return 1;
        }
        if (ev->kind != EV_READ && ev->first == first) {
            return 0; /* (drawn, or uploaded, before it is read) */
        }
    }
    return 0;
}

/* Which copies of the frame the pictures between make again (Flat's copy_ok) */
static void flat_copies(Flat *f, const Uploads *u) {
    size_t c, e;

    if (f->copy_room < u->ncopies) {
        f->copy_room = u->ncopies * 2;
        f->copy_ok = realloc(f->copy_ok, f->copy_room);
        if (f->copy_ok == NULL) {
            PORT_FATAL("out of memory");
        }
    }
    f->ncopies = u->ncopies;
    for (c = 0; c < u->ncopies; c++) {
        const Event *copy = &u->ev[u->copies[c]];
        int read_after = 0, read_before = 0;

        for (e = 0; e < u->nev; e++) {
            const Event *ev = &u->ev[e];

            if (ev->kind == EV_READ && ev->first <= copy->last && ev->last >= copy->first) {
                if (e < u->copies[c]) {
                    read_before = 1;
                } else {
                    read_after = 1;
                }
            }
        }
        f->copy_ok[c] = (uint8_t)(read_after && !read_before);
    }
}

/* Pairs this frame's items (f) with the frame before's (o): in order, each with the first alike among
   the next few of the frame before not taken yet */
static void flat_pair(Flat *f, const Flat *o, const Uploads *u) {
    size_t i, at = 0;

    for (i = 0; i < f->nitems; i++) {
        Item *b = &f->items[i];
        size_t k;

        b->pair = -1;
        if (o == NULL) {
            continue;
        }
        for (k = at; k < o->nitems && k < at + 48; k++) {
            if (items_alike(o, &o->items[k], f, b)) {
                if (items_pair(o, &o->items[k], f, b, &b->wrap_s, &b->wrap_t)) {
                    b->pair = (int32_t)k;
                }
                at = k + 1;
                break;
            }
        }
        if (b->pair >= 0 && b->fbp != f->draw_fbp && u != NULL && read_first(u, b->fbp)) {
            /* drawn into another buffer (a scrolling texture's) that the frame reads before it draws into
               it: the pictures between leave it as the game's frame has it (port_interp_end_vblank) */
            f->restore = 1;
        }
    }
}

/* Each run of this frame (o) with the one of the frame before (prev) that is it: by its batch, the nearest
   with the same, it the nearest to that one in turn (objects_find); then, among the runs neither pairs
   that way (what the CPU makes anew each frame: an effect's sprites, a model it moves vertex by vertex,
   whose batch is new every frame), in their order, each with the first of the frame before's within
   the next few that is drawn with the same primitive and texture, as many vertices, in the same
   section, its middle within INTERP_MAX_MOVE. run_motion then takes the pair only if no vertex moved
   more than that, and the place between does not cut past the eye. */
static uint64_t stat_runs, stat_runs_key, stat_runs_order;

static void objects_pair(Objects *o, const Objects *prev) {
    size_t i, at = 0, k;
    uint8_t *taken;

    for (i = 0; i < o->nruns; i++) {
        o->runs[i].pair = -1;
    }
    if (prev == NULL || prev->nruns == 0) {
        return;
    }
    taken = calloc(prev->nruns, 1);
    if (taken == NULL) {
        PORT_FATAL("out of memory");
    }
    for (i = 0; i < o->nruns; i++) {
        Run *r = &o->runs[i];
        const Run *a = objects_find(prev, r);

        stat_runs += r->placed;
        if (a != NULL && objects_find(o, a) == r) {
            r->pair = (int32_t)(a - prev->runs);
            taken[r->pair] = 1;
            stat_runs_key++;
        }
    }
    for (i = 0; i < o->nruns; i++) {
        Run *r = &o->runs[i];

        if (r->pair >= 0 || !r->placed || r->section == SECTION_OTHER || r->section == SECTION_REFLECT) {
            continue;
        }
        for (k = at; k < prev->nruns && k < at + 32; k++) {
            const Run *a = &prev->runs[k];
            double d2 = 0;
            int c;

            if (taken[k] || !a->placed || a->section != r->section || a->count != r->count || a->prim != r->prim ||
                a->tex != r->tex) {
                continue;
            }
            for (c = 0; c < 3; c++) {
                d2 += (a->middle[c] - r->middle[c]) * (a->middle[c] - r->middle[c]);
            }
            if (d2 < INTERP_MAX_MOVE * INTERP_MAX_MOVE) {
                r->pair = (int32_t)k;
                taken[k] = 1;
                at = k + 1;
                stat_runs_order++;
            }
            break;
        }
    }
    free(taken);
}

typedef struct {
    uint32_t left, flg, nreg, reg, dwords;
    uint64_t regs;
    uint32_t prim;
    uint8_t *st;      /* PACKED: the ST qword before the next vertex, in this record */
    uint8_t *rgbaq;   /* PACKED: the RGBAQ qword before the next vertex, in this record */
    uint8_t *uv;      /* PACKED: the UV qword before the next vertex, in this record */
    int adc_left;     /* vertices still to drop after one the new camera cannot see */
    int item_open;    /* this tag's 2D vertices are an item (the first has come) */
    int dropping;     /* REPLAY: this IMAGE tag's data is an upload left out */
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
    Screen screen_reflect; /* the reflection's projection (SECTION_REFLECT) */
    float reflect_view[12];
    uint32_t reflect_fbp;  /* the reflection's target */
    int have_reflect;      /* there is one, by the world's camera */
    uint32_t draw_fbp; /* SCAN: FRAME_1's FBP at the first world mark: the buffer drawn */
    /* the GS registers as the game wrote them */
    uint64_t frame[2], tex0[2], bitbltbuf, prmodecont, prmode;
    int frame_set[2], tex0_set[2], bitbltbuf_set;
    uint64_t trxpos, trxreg, scissor[2], zbuf[2], tex1[2];
    /* the pages read and written (SCAN), and the uploads */
    Uploads *uploads;    /* SCAN: this frame's events */
    uint32_t upload;     /* the next upload's number in the frame */
    uint32_t copy;       /* the next copy of the frame's */
    const Flat *copies;  /* REPLAY: the frame's, for its copies (NULL: none made) */
    int ask_uploads;     /* REPLAY: the native GS has the frame's uploads: ask for them again */
    int skip_image;      /* REPLAY: the next IMAGE data is an upload asked for again: left out */
    uint64_t upload_bytes_left_out;
    /* the 2D (Flat): SCAN keeps this frame's items; REPLAY moves the paired ones */
    Flat *flat;          /* SCAN: this frame's; REPLAY: the pending frame's (NULL: none moved) */
    const Flat *flat_prev; /* REPLAY: the frame's before it */
    long item;           /* the item of the last 2D vertex, -1 before the first */
    uint32_t item_vertex; /* the next vertex's number in it */
    uint64_t items_moved;
    /* the runs: SCAN adds them to objects; both walks number them the same way */
    Objects *objects;    /* SCAN: this frame's (or NULL); REPLAY: the pending frame's */
    const Objects *prev; /* REPLAY: the frame's before it, or NULL */
    uint32_t serial;     /* the run of the last 3D record */
    long run;            /* its number in this walk, -1 before the first */
    uint32_t run_vertex; /* the next vertex's number in the run */
    const Run *run_prev; /* REPLAY: the same run in the frame before, when there is one */
    int run_moving;      /* REPLAY: it moved */
    /* the run's points so far with a Q that is 1/w: X and Y, Z, and the depth (vertex_view) */
#define SEEN 128
    uint64_t seen_xy[SEEN];
    double seen_z[SEEN], seen_depth[SEEN];
    int nseen;
    double run_most;     /* REPLAY: the most a vertex of it without ADC moved */
    double eye_move[3];  /* REPLAY: how far the camera moved from the frame before to this one */
    /* REPLAY */
    uint32_t shown_fbp; /* the buffer the replay draws into, in place of draw_fbp */
    double t;           /* how far between the frame shown (0) and the one drawn (1) the picture is */
    int exact;          /* the frame as it was, for the buffers other than the frame (port_interp_end_vblank) */
    int prev_frame_kept; /* the native GS keeps the frame shown before the pictures between (take_snapshot) */
    int record_reads_prev; /* REPLAY: this record reads the frame shown as a texture (the native GS: read the copy) */
    Move move;          /* the frame's view to the camera between */
    Camera mid;         /* the camera between */
    uint64_t moved, dropped, objects_moved[SECTIONS]; /* vertices moved, dropped (ADC), moved as their model moved (by section) */
} Walk;

/* A primitive draws into the frame: its context's FRAME is the buffer drawn (PRIM's CTXT, or PRMODE's
   when PRMODECONT says so). Only those vertices are moved: whatever else the world draws into (none
   seen so far) is drawn again as it was. */
static int draws_into(const Walk *w, uint32_t prim, uint32_t fbp) {
    int n = (w->prmodecont & 1) ? (int)((prim >> 9) & 1) : (int)((w->prmode >> 9) & 1);

    return w->frame_set[n] && (w->frame[n] & 0x1FF) == fbp;
}

static int draws_frame(const Walk *w, uint32_t prim) {
    return draws_into(w, prim, w->draw_fbp);
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
    if (bits == PORT_FRAME_INTERP_ARM) {
        w->section = w->world_seen ? SECTION_ARM : SECTION_NONE;
        return;
    }
    if ((bits & 0xFF) != 0) {
        /* the 256x256 view of the reflection (mode 1, the mirror image), or another projection */
        w->section = (bits >> 8) == 1 ? SECTION_REFLECT : SECTION_OTHER;
        if (w->section == SECTION_REFLECT && w->mode == SCAN && !w->have_reflect) {
            const float *s = &w->fields[PORT_FRAME_INTERP_SCREEN];

            memcpy(w->reflect_view, &w->fields[PORT_FRAME_INTERP_VIEW], sizeof(w->reflect_view));
            w->screen_reflect.sx = s[0];
            w->screen_reflect.sy = s[1];
            w->screen_reflect.cx = s[2];
            w->screen_reflect.cy = s[3];
            w->screen_reflect.cz = s[4];
            w->screen_reflect.az = s[5];
            w->reflect_fbp = w->frame_set[0] ? (uint32_t)(w->frame[0] & 0x1FF) : 0xFFFFFFFFu;
            w->have_reflect = 1;
        }
        return;
    }
    if ((bits >> 8) == 2) {
        w->section = w->world_seen ? SECTION_EYE : SECTION_OTHER; /* models before the eye */
        return;
    }
    w->section = SECTION_WORLD;
    if (w->mode == SCAN && !w->world_seen) {
        const float *s = &w->fields[PORT_FRAME_INTERP_SCREEN];

        camera_from_fields(&w->camera, &w->fields[PORT_FRAME_INTERP_VIEW]);
        if (w->have_reflect) {
            /* the reflection's view moves with the world's when it is the world's camera */
            int k;

            for (k = 0; k < 12; k++) {
                if (fabsf(w->reflect_view[k] - w->fields[PORT_FRAME_INTERP_VIEW + k]) > 1e-3f * (1.0f + fabsf(w->reflect_view[k]))) {
                    w->have_reflect = 0;
                }
            }
        }
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

/* A vertex in the camera's space of its frame (v), from its screen place and depth: 0 if it has none
   (behind the camera, or no Q and no Z to tell) */
/* The vertex's Q is 1/w: textured by ST, and not 1.0, which is the Q of the sphere-mapped second pass
   of program 0x280 (its ST is a direction, not a place on the model: no depth in it) */
static int perspective_q(const GifWalk *p) {
    float sq[3];

    if (p->st == NULL || !(p->prim & 0x10) || (p->prim & 0x100)) {
        return 0;
    }
    memcpy(sq, p->st, 12);
    return sq[2] > 0 && isfinite(sq[2]) && sq[2] != 1.0f;
}

static int vertex_view(Walk *w, const GifWalk *p, const uint8_t *q, int xyzf, double v[3], double *depth_out,
                       double *z_out) {
    const Screen *s = &w->screen;
    uint64_t lo, hi;
    double X, Y, z, depth = 0;
    float sq[3];

    memcpy(&lo, q, 8);
    memcpy(&hi, q + 8, 8);
    X = (double)(lo & 0xFFFF) / 16.0;
    Y = (double)((lo >> 32) & 0xFFFF) / 16.0;
    z = xyzf ? (double)((hi >> 4) & 0xFFFFFF) : (double)(uint32_t)hi;
    if (perspective_q(p)) {
        memcpy(sq, p->st, 12);
        depth = 1.0 / sq[2];
        if (w->nseen < SEEN) {
            /* (for a second pass over the same points) */
            w->seen_xy[w->nseen] = lo & 0x0000FFFF0000FFFFull;
            w->seen_z[w->nseen] = z;
            w->seen_depth[w->nseen++] = depth;
        }
    } else {
        /* A point the run drew before with a Q that is 1/w (the second pass of program 0x280 goes over
           the first's points): that depth. The Z alone can be off: the arm's Z is pulled near. */
        int i;

        for (i = 0; i < w->nseen; i++) {
            if (w->seen_xy[i] == (lo & 0x0000FFFF0000FFFFull) && w->seen_z[i] == z) {
                depth = w->seen_depth[i];
                break;
            }
        }
    }
    if (depth == 0 && z - s->cz > 0) {
        depth = s->az / (z - s->cz);
    }
    if (!(depth > 0) || !isfinite(depth) || s->sx == 0 || s->sy == 0) {
        return 0;
    }
    v[0] = (X - s->cx) * depth / s->sx;
    v[1] = (Y - s->cy) * depth / s->sy;
    v[2] = depth;
    *depth_out = depth;
    *z_out = z;
    return 1;
}

/* The place a vertex is kept as: in the world, or before the eye in the camera's space */
static void view_to_place(const Walk *w, int section, const double v[3], double place[3]) {
    const Camera *c = &w->camera;
    int i;

    if (section == SECTION_EYE) {
        memcpy(place, v, sizeof(double) * 3);
        return;
    }
    for (i = 0; i < 3; i++) {
        place[i] = c->r[0][i] * (v[0] - c->t[0]) + c->r[1][i] * (v[1] - c->t[1]) + c->r[2][i] * (v[2] - c->t[2]);
    }
}

/* SCAN: a vertex of a 3D run, kept as its place */
static void keep_vertex(Walk *w, const GifWalk *p, const uint8_t *q, int xyzf) {
    double v[3], place[3] = {0, 0, 0}, depth, z;
    const Run *r = &w->objects->runs[w->objects->nruns - 1];
    int valid = r->section == SECTION_EYE || r->section == SECTION_ARM || r->section == SECTION_WORLD;

    uint64_t hi;

    /* A vertex with ADC (the first two of every strip, and those around one the VU1 program clipped):
       VERTEX_MAYBE */
    memcpy(&hi, q + 8, 8);
    valid = valid && vertex_view(w, p, q, xyzf, v, &depth, &z);
    if (valid) {
        view_to_place(w, r->section, v, place);
    }
    objects_add_vertex(w->objects, place, !valid ? 0 : ((hi >> 47) & 1) ? VERTEX_MAYBE : VERTEX_SURE);
}

/* REPLAY, at a run's start: whether it moved since the frame before, from its vertices without ADC:
   the most any of them moved (run_most), against a 500th of the middle's distance from the camera;
   or, in the world, all of them as far as the camera moved, and the same way: a model that goes with
   the camera. The sky's dome on map 000 is one: its strips, a few thousand to twenty thousand away,
   go with the player, and those nearer than a 500th of their move (6,000 at a walking step of 12)
   were moved with it while the others stayed in the world, half a step behind in the picture between:
   thin lines along the strips' shared edges, dark (a gap in the additive sky) walking forward and
   bright (two strips over each other) walking back. A model that goes with the camera is moved with
   its model whatever its distance, so the strips stay together. (A model that does not move taken
   for one that goes with the camera, far away while the player creeps, is drawn half way between its
   two places, which are the same: no harm.) */
static void run_motion(Walk *w) {
    const Objects *o = w->objects;
    const Run *cur, *prev = w->run_prev;
    double most = 0, dist2 = 0, sum[3] = {0, 0, 0};
    uint32_t k, n = 0;
    int i;

    w->run_moving = 0;
    w->run_most = 0;
    if (prev == NULL) {
        return;
    }
    cur = &o->runs[w->run];
    for (k = 0; k < cur->count; k++) {
        const double *a = w->prev->pos[prev->first + k], *b = o->pos[cur->first + k];
        double d2 = 0;

        if (o->valid[cur->first + k] != VERTEX_SURE || w->prev->valid[prev->first + k] != VERTEX_SURE) {
            continue;
        }
        for (i = 0; i < 3; i++) {
            d2 += (b[i] - a[i]) * (b[i] - a[i]);
            sum[i] += b[i] - a[i];
        }
        n++;
        most = d2 > most ? d2 : most;
        {
            /* the place between (half way at 60), on the line between, must not come much nearer the eye
               than both ends: the arm's swing turns a long way round the shoulder, by the eye, in one frame */
            double mid[3], za, zb, zm;

            for (i = 0; i < 3; i++) {
                mid[i] = a[i] + (b[i] - a[i]) * w->t;
            }
            if (cur->section == SECTION_EYE) {
                za = a[2];
                zb = b[2];
                zm = mid[2];
            } else {
                za = w->camera.r[2][0] * a[0] + w->camera.r[2][1] * a[1] + w->camera.r[2][2] * a[2] + w->camera.t[2];
                zb = w->camera.r[2][0] * b[0] + w->camera.r[2][1] * b[1] + w->camera.r[2][2] * b[2] + w->camera.t[2];
                zm = w->mid.r[2][0] * mid[0] + w->mid.r[2][1] * mid[1] + w->mid.r[2][2] * mid[2] + w->mid.t[2];
            }
            if (zm < 0.75 * (za < zb ? za : zb)) {
                w->run_prev = NULL;
                return;
            }
        }
    }
    most = sqrt(most);
    if (most > INTERP_MAX_MOVE) {
        w->run_prev = NULL; /* two things that share a batch */
        return;
    }
    if (cur->section == SECTION_EYE) {
        dist2 = cur->middle[0] * cur->middle[0] + cur->middle[1] * cur->middle[1] + cur->middle[2] * cur->middle[2];
    } else {
        for (i = 0; i < 3; i++) {
            dist2 += (cur->middle[i] - w->camera.eye[i]) * (cur->middle[i] - w->camera.eye[i]);
        }
    }
    w->run_most = most;
    w->run_moving = most > sqrt(dist2) * 0.002;
    if (!w->run_moving && cur->section == SECTION_WORLD && n > 0) {
        /* with the camera: every vertex moved by the camera's move, within a tenth of it */
        double step = sqrt(w->eye_move[0] * w->eye_move[0] + w->eye_move[1] * w->eye_move[1] +
                           w->eye_move[2] * w->eye_move[2]),
               off = 0;

        for (i = 0; i < 3; i++) {
            off += (sum[i] / n - w->eye_move[i]) * (sum[i] / n - w->eye_move[i]);
        }
        /* (a vertex's place, from its screen place and Q, is off by up to about a 10,000th of its
           distance: the dome's far strips, 20,000 away, by 1 to 2) */
        double noise = sqrt(dist2) * 0.0001;

        w->run_moving = step > 1.0 && sqrt(off) <= step * 0.1 + noise && most <= step * 1.1 + 2 * noise;
    }
}

static double distance(const double *a, const double *b) {
    return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

/* REPLAY: where a vertex of a moving model is half way, in the camera's space between (m); 0 when its
   run is not paired or did not move. A vertex with ADC in either frame may have been clipped in the
   other (its place there wrapped round) while the triangles of this frame use it: it is taken as it is
   only when it is as far from its run's middle in both frames, as a model's points are; otherwise it
   moves by half its run's middle's move. */
static int object_between(Walk *w, double m[3]) {
    const Objects *o = w->objects;
    const Run *cur, *prev = w->run_prev;
    const double *a, *b;
    double mid[3];
    uint32_t k = w->run_vertex;
    int i;

    if (prev == NULL || !w->run_moving) {
        return 0;
    }
    cur = &o->runs[w->run];
    if (k >= cur->count || !o->valid[cur->first + k]) {
        return 0;
    }
    a = w->prev->pos[prev->first + k];
    b = o->pos[cur->first + k];
    if (w->prev->valid[prev->first + k] == VERTEX_SURE && o->valid[cur->first + k] == VERTEX_SURE) {
        for (i = 0; i < 3; i++) {
            mid[i] = a[i] + (b[i] - a[i]) * w->t;
        }
    } else {
        double ra = distance(a, prev->middle), rb = distance(b, cur->middle);
        double most = ra > rb ? ra : rb;

        if (w->prev->valid[prev->first + k] && fabs(ra - rb) <= most * 0.25 + 2.0) {
            for (i = 0; i < 3; i++) {
                mid[i] = a[i] + (b[i] - a[i]) * w->t;
            }
        } else {
            for (i = 0; i < 3; i++) {
                mid[i] = b[i] + (prev->middle[i] - cur->middle[i]) * (1.0 - w->t);
            }
        }
    }
    if (cur->section == SECTION_EYE) {
        memcpy(m, mid, sizeof(mid));
    } else {
        for (i = 0; i < 3; i++) {
            m[i] = w->mid.r[i][0] * mid[0] + w->mid.r[i][1] * mid[1] + w->mid.r[i][2] * mid[2] + w->mid.t[i];
        }
    }
    return 1;
}

/* A vertex of the world (PACKED XYZF2 or XYZ2, q its qword) seen from the camera between: moved with
   its model when it is paired, else with the camera (the world) or not at all (before the eye, the arm) */
static void move_vertex_in(Walk *w, GifWalk *p, uint8_t *q, int xyzf, int section, uint64_t lo, uint64_t hi, float *sq);

static void move_vertex(Walk *w, GifWalk *p, uint8_t *q, int xyzf, int section) {
    uint64_t lo, hi;
    float sq[3] = {0, 0, 0};

    memcpy(&lo, q, 8);
    memcpy(&hi, q + 8, 8);
    if (p->st != NULL && (p->prim & 0x10) && !(p->prim & 0x100)) {
        memcpy(sq, p->st, 12);
    }
    if (section == SECTION_REFLECT) {
        /* (by the reflection's projection, below too) */
        Screen world = w->screen;

        w->screen = w->screen_reflect;
        move_vertex_in(w, p, q, xyzf, section, lo, hi, sq);
        w->screen = world;
        return;
    }
    move_vertex_in(w, p, q, xyzf, section, lo, hi, sq);
}

static void move_vertex_in(Walk *w, GifWalk *p, uint8_t *q, int xyzf, int section, uint64_t lo, uint64_t hi, float *sq) {
    const Screen *s = &w->screen;
    double z, depth = 0, v[3], m[3], nx, ny, nz;
    int64_t ix, iy;
    int i, drop = 0;

    if (!vertex_view(w, p, q, xyzf, v, &depth, &z)) {
        return;
    }
    if (section != SECTION_REFLECT && object_between(w, m)) {
        w->objects_moved[section]++;
    } else if (section == SECTION_WORLD || section == SECTION_REFLECT) {
        for (i = 0; i < 3; i++) {
            m[i] = w->move.r[i][0] * v[0] + w->move.r[i][1] * v[1] + w->move.r[i][2] * v[2] + w->move.t[i];
        }
    } else {
        return;
    }
    if (!(m[2] > 0)) {
        drop = 1;
    } else {
        /* clipped as the VU1 programs clip: past the guard band, or Z below 0 (beyond the far plane) */
        nx = s->sx * m[0] / m[2] + s->cx;
        ny = s->sy * m[1] / m[2] + s->cy;
        /* Z is cz + k / w, k the projection's az for the world. Program 0x700's effects (the
           flames and the lights' glows, drawn with a GEQUAL test and no Z write) have a k of their
           own, about 16 times the world's (the glow on map 000 at w 4,700: Z 3,538 where the world
           there has 206), which pulls them in front. So k is taken from the vertex itself, (z - cz) w:
           moving such a vertex by the world's az alone moved its Z a 16th of the way, and the glows
           came through the walls before them in the pictures between (the lights flickered as the
           player walked toward them). For the world k is az, as before. */
        nz = s->cz + (z - s->cz) * depth / m[2];
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
            if (perspective_q(p)) {
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
        if (w->mode == REPLAY && w->prev_frame_kept && (*value & 0x3FFF) == w->shown_fbp * 32) {
            w->record_reads_prev = 1;
        }
        if (w->mode == SCAN && w->uploads != NULL) {
            /* the texture (twice its first level: its mipmaps after it) and the CLUT are read */
            uint32_t first, last;
            uint64_t v = *value;

            pages_of((uint32_t)(v & 0x3FFF), (uint32_t)((v >> 14) & 0x3F), (uint32_t)((v >> 20) & 0x3F),
                     (((w->tex1[n] >> 2) & 7) ? 2u : 1u) << ((v >> 30) & 0xF), &first, &last);
            uploads_event(w->uploads, EV_READ, first, last);
            pages_of((uint32_t)((v >> 37) & 0x3FFF), 1, 0, 16, &first, &last);
            uploads_event(w->uploads, EV_READ, first, last);
        }
        if (w->mode == REPLAY && (*value & 0x3FFF) == w->draw_fbp * 32) {
            *value = (*value & ~(uint64_t)0x3FFF) | (w->shown_fbp * 32);
        }
        break;
    }
    case GS_PRMODECONT:
        w->prmodecont = *value;
        break;
    case 0x14: /* TEX1_1 */
    case 0x15:
        w->tex1[r - 0x14] = *value;
        break;
    case 0x40: /* SCISSOR_1 */
    case 0x41:
        w->scissor[r - 0x40] = *value;
        break;
    case 0x4E: /* ZBUF_1 */
    case 0x4F:
        w->zbuf[r - 0x4E] = *value;
        if (w->mode == SCAN && w->uploads != NULL && !((*value >> 32) & 1)) {
            /* (Z written: as wide as the FRAME of its context) */
            uint32_t first, last, n = r - 0x4E;
            uint32_t rows = w->scissor[n] ? (uint32_t)((w->scissor[n] >> 48) & 0x7FF) + 1 : 512;

            pages_of((uint32_t)(*value & 0x1FF) * 32, (uint32_t)((w->frame[n] >> 16) & 0x3F),
                     0x30 | (uint32_t)((*value >> 24) & 0xF), rows, &first, &last);
            uploads_event(w->uploads, EV_WRITE, first, last);
        }
        break;
    case 0x51: /* TRXPOS */
        w->trxpos = *value;
        break;
    case 0x52: /* TRXREG */
        w->trxreg = *value;
        break;
    case GS_PRMODE:
        w->prmode = *value;
        break;
    case GS_FRAME_1:
    case GS_FRAME_2: {
        int n = (int)(r - GS_FRAME_1);

        w->frame[n] = *value;
        w->frame_set[n] = 1;
        if (w->mode == SCAN && w->uploads != NULL) {
            /* drawn into, to the scissor's bottom (or 512 rows) */
            uint32_t first, last, rows = w->scissor[n] ? (uint32_t)((w->scissor[n] >> 48) & 0x7FF) + 1 : 512;

            pages_of((uint32_t)(*value & 0x1FF) * 32, (uint32_t)((*value >> 16) & 0x3F), (uint32_t)((*value >> 24) & 0x3F),
                     rows, &first, &last);
            uploads_event(w->uploads, EV_WRITE, first, last);
        }
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
    case GS_TRXDIR: {
        uint32_t dbp = (uint32_t)(w->bitbltbuf >> 32) & 0x3FFF;
        uint32_t dir = (uint32_t)(*value & 3);

        int frame_copy = dir == 2 && dbp != w->draw_fbp * 32 && (uint32_t)(w->bitbltbuf & 0x3FFF) == w->draw_fbp * 32;

        if (w->mode == SCAN && w->uploads != NULL && (dir == 0 || dir == 2)) {
            /* written: from the destination's first block, to the rows the transfer reaches */
            uint32_t first, last;
            Uploads *u = w->uploads;

            pages_of(dbp, (uint32_t)(w->bitbltbuf >> 48) & 0x3F, (uint32_t)(w->bitbltbuf >> 56) & 0x3F,
                     (uint32_t)((w->trxpos >> 48) & 0x7FF) + (uint32_t)((w->trxreg >> 32) & 0xFFF), &first, &last);
            uploads_event(u, dir == 0 ? EV_UPLOAD : EV_WRITE, first, last);
            if (frame_copy) {
                if (u->ncopies == u->copies_room) {
                    u->copies_room = u->copies_room ? u->copies_room * 2 : 16;
                    u->copies = realloc(u->copies, u->copies_room * sizeof(*u->copies));
                    if (u->copies == NULL) {
                        PORT_FATAL("out of memory");
                    }
                }
                u->copies[u->ncopies++] = (uint32_t)(u->nev - 1);
            }
        }
        if (w->mode == REPLAY && frame_copy && w->copies != NULL && w->copy < w->copies->ncopies && w->copies->copy_ok[w->copy]) {
            /* a copy of the frame the frame reads: made from the picture between (BITBLTBUF's source is the
               buffer shown) */
        } else if (w->mode == REPLAY) {
            /* (bitbltbuf is the game's: a transfer into the buffer drawn goes into the one shown) */
            if (dir == 1 || (dir == 2 && dbp != w->draw_fbp * 32)) {
                *reg = PORT_GS_REG_FRAME_INTERP;
                *value = (uint64_t)0xFF << 32;
            } else if (dir == 0 && w->ask_uploads && dbp != w->draw_fbp * 32) {
                /* the native GS asked for the frame's upload again (it kept its pages, or its bytes):
                   the data left out (walk_record) */
                *reg = PORT_GS_REG_REPLAY_UPLOAD;
                *value = (uint64_t)w->upload | (uint64_t)(uint32_t)(w->bitbltbuf >> 32) << 32;
                w->skip_image = 1;
            }
        }
        if (dir == 0) {
            w->upload++;
        }
        w->copy += frame_copy;
        break;
    }
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

/* SCAN: a 2D vertex kept for the pairs (its place, and its colour and texture coordinates when they are
   in this tag's qwords, where a picture between can change them) */
static void keep_flat_vertex(Walk *w, GifWalk *p, const uint8_t *q) {
    FlatVertex *v = flat_add_vertex(w->flat);
    uint64_t lo;

    memcpy(&lo, q, 8);
    v->x = (int32_t)(lo & 0xFFFF);
    v->y = (int32_t)((lo >> 32) & 0xFFFF);
    v->has_rgba = p->rgbaq != NULL;
    if (p->rgbaq != NULL) {
        uint32_t c[4];

        memcpy(c, p->rgbaq, 16);
        v->rgba[0] = (uint8_t)c[0];
        v->rgba[1] = (uint8_t)c[1];
        v->rgba[2] = (uint8_t)c[2];
        v->rgba[3] = (uint8_t)c[3];
    }
    v->coords = 0;
    v->s = v->t = 0;
    if ((p->prim & 0x10) && !(p->prim & 0x100) && p->st != NULL) {
        float st[2];

        memcpy(st, p->st, 8);
        v->coords = 1;
        v->s = st[0];
        v->t = st[1];
    } else if ((p->prim & 0x10) && (p->prim & 0x100) && p->uv != NULL) {
        uint32_t uv[2];

        memcpy(uv, p->uv, 8);
        v->coords = 2;
        v->s = (float)(uv[0] & 0x3FFF) / 16.0f;
        v->t = (float)(uv[1] & 0x3FFF) / 16.0f;
    }
}

/* A paired 2D vertex's alpha at the picture's time */
static double vb_alpha_between(const Walk *w, const Item *b) {
    const Item *a = &w->flat_prev->items[b->pair];
    double va = w->flat_prev->verts[a->first + w->item_vertex].rgba[3], vb = w->flat->verts[b->first + w->item_vertex].rgba[3];

    return vb + (va - vb) * (1.0 - w->t);
}

/* REPLAY: a paired 2D vertex where it is at the picture's time (t of the way from the frame before's) */
static void move_flat_vertex(Walk *w, GifWalk *p, uint8_t *q) {
    const Item *b = &w->flat->items[w->item];
    const Item *a;
    const FlatVertex *va, *vb;
    double u = 1.0 - w->t;
    uint64_t lo;

    if (w->item_vertex >= b->count) {
        return;
    }
    if (w->prev_frame_kept && (b->prim & 0x10) && b->fbp == w->flat->draw_fbp &&
        (uint32_t)(b->tex & 0x3FFF) == w->flat->shown_fbp * 32 && p->rgbaq != NULL) {
        /* The frame shown before blended in (Game_BlendPreviousFrame: ALPHA 0x44, As of 0x80 from it):
           P = a Prev + (1 - a) Drawn, which decays the frames before by a each frame. A picture between at
           t blends in the frame shown before it (the native GS's copy of it, take_snapshot) by a^t: from
           the frame shown at t 0 to the game's own blend at t 1, with no jump at either; the trail as long
           in time as the game's. (Blending each picture with the one before instead, by the weight that
           gives the same lag, moved evenly between the frames but jumped at each: the game's frame is
           blended with the frame before, not with the last picture between.) */
        uint32_t c[4];
        double a, base = b->pair >= 0 && w->flat_prev != NULL
                             ? vb_alpha_between(w, b)
                             : (double)w->flat->verts[b->first + w->item_vertex].rgba[3];

        memcpy(c, p->rgbaq, 16);
        a = base / 128.0;
        a = a <= 0 ? 0 : a >= 1 ? 1 : pow(a, w->t);
        c[3] = (uint32_t)floor(a * 128.0 + 0.5);
        memcpy(p->rgbaq, c, 16);
        w->items_moved++;
        return;
    }
    if (b->pair < 0 || w->flat_prev == NULL) {
        return;
    }
    a = &w->flat_prev->items[b->pair];
    va = &w->flat_prev->verts[a->first + w->item_vertex];
    vb = &w->flat->verts[b->first + w->item_vertex];
    memcpy(&lo, q, 8);
    lo = (lo & ~(uint64_t)0xFFFF & ~((uint64_t)0xFFFF << 32)) |
         (uint64_t)(uint32_t)(vb->x + (int32_t)floor((va->x - vb->x) * u + 0.5)) |
         ((uint64_t)(uint32_t)(vb->y + (int32_t)floor((va->y - vb->y) * u + 0.5)) << 32);
    memcpy(q, &lo, 8);
    if (p->rgbaq != NULL && va->has_rgba && vb->has_rgba) {
        uint32_t c[4];
        int k;

        memcpy(c, p->rgbaq, 16);
        for (k = 0; k < 4; k++) {
            c[k] = (uint32_t)(vb->rgba[k] + (int)floor((va->rgba[k] - vb->rgba[k]) * u + 0.5));
        }
        memcpy(p->rgbaq, c, 16);
    }
    if (vb->coords == 1 && p->st != NULL) {
        float st[2];

        st[0] = (float)(vb->s + (va->s + b->wrap_s - vb->s) * u);
        st[1] = (float)(vb->t + (va->t + b->wrap_t - vb->t) * u);
        memcpy(p->st, st, 8);
    } else if (vb->coords == 2 && p->uv != NULL) {
        uint32_t uv[2];
        double s = vb->s + (va->s + b->wrap_s - vb->s) * u, t = vb->t + (va->t + b->wrap_t - vb->t) * u;

        memcpy(uv, p->uv, 8);
        uv[0] = (uv[0] & ~0x3FFFu) | ((uint32_t)floor(s * 16.0 + 0.5) & 0x3FFF);
        uv[1] = (uv[1] & ~0x3FFFu) | ((uint32_t)floor(t * 16.0 + 0.5) & 0x3FFF);
        memcpy(p->uv, uv, 8);
    }
    w->items_moved++;
}

/* One record: in REPLAY, data is the copy to send, and is changed (uploads left out are taken out of
   it). Returns the bytes left. */
static size_t walk_record(Walk *w, const Record *r, uint8_t *data) {
    int path = (int)(r->what & 0xFF), is_3d = (r->what & RECORD_3D) != 0;
    GifWalk *p = &w->paths[path];
    size_t count = r->bytes / 16, i = 0, out = 0;

    p->st = NULL;
    p->rgbaq = NULL;
    p->uv = NULL;
    if (is_3d && (w->run < 0 || r->serial != w->serial)) {
        /* a new VU1 run */
        w->serial = r->serial;
        w->run++;
        w->run_vertex = 0;
        w->nseen = 0;
        if (w->mode == SCAN && w->objects != NULL) {
            objects_add_run(w->objects, r->key, w->section);
        } else if (w->mode == REPLAY) {
            const Objects *o = w->objects;

            /* (paired once for the frame: objects_pair) */
            w->run_prev = o != NULL && w->prev != NULL && (size_t)w->run < o->nruns && o->runs[w->run].pair >= 0
                              ? &w->prev->runs[o->runs[w->run].pair]
                              : NULL;
            run_motion(w);
        }
    }
    while (i < count) {
        uint8_t *q = data + i * 16;
        size_t n = 1; /* qwords looked at */
        int keep = 1;

        if (p->left == 0) {
            uint64_t tag, regs;
            uint32_t nloop;

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
            p->item_open = 0;
            p->dropping = 0;
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
                if (w->mode == REPLAY && w->skip_image && nloop != 0) {
                    /* an upload left out: its data, behind a tag with none (which keeps its EOP) */
                    w->skip_image = 0;
                    p->dropping = 1;
                    tag &= ~(uint64_t)0x7FFF;
                    memcpy(q, &tag, 8);
                    w->upload_bytes_left_out += (uint64_t)nloop * 16;
                }
            }
        } else if (p->flg == 0) {
            uint32_t d = (uint32_t)(p->regs >> (p->reg * 4)) & 0xF;
            uint64_t v[2];

            memcpy(v, q, 16);
            switch (d) {
            case 0x0:
                p->prim = (uint32_t)(v[0] & 0x7FF);
                break;
            case 0x1:
                p->rgbaq = q;
                break;
            case 0x2:
                p->st = q;
                break;
            case 0x3:
                p->uv = q;
                break;
            case 0x4:
            case 0x5:
                if (is_3d) {
                    int section = w->section;
                    int tri = (p->prim & 7) >= 3 && (p->prim & 7) <= 5;
                    int moves = ((section == SECTION_WORLD || section == SECTION_EYE || section == SECTION_ARM) && tri &&
                                 draws_frame(w, p->prim)) ||
                                (section == SECTION_REFLECT && tri && w->have_reflect && draws_into(w, p->prim, w->reflect_fbp));

                    if (w->mode == SCAN && w->objects != NULL) {
                        Run *run = &w->objects->runs[w->objects->nruns - 1];

                        if (run->count == 0) {
                            /* what it is drawn with */
                            int c = (w->prmodecont & 1) ? (int)((p->prim >> 9) & 1) : (int)((w->prmode >> 9) & 1);

                            run->prim = p->prim;
                            run->tex = (p->prim & 0x10) ? w->tex0[c] & ~((uint64_t)7 << 61) : 0;
                        }
                        if (moves) {
                            keep_vertex(w, p, q, d == 0x4);
                        } else {
                            static const double none[3] = {0, 0, 0};

                            objects_add_vertex(w->objects, none, 0);
                        }
                    } else if (w->mode == REPLAY && w->exact) {
                        /* (drawing the frame again for the other buffers: its 3D into the frame is not
                           drawn, every vertex with ADC) */
                        if ((p->prim & 7) >= 3 && (p->prim & 7) <= 5 && draws_frame(w, p->prim)) {
                            q[15] |= 0x80;
                        }
                    } else if (w->mode == REPLAY && moves) {
                        move_vertex(w, p, q, d == 0x4, section);
                    }
                    w->run_vertex++;
                } else if (w->flat != NULL && (p->prim & 7) != 7) {
                    /* a 2D vertex: the first of its tag begins an item */
                    if (!p->item_open) {
                        int c = (w->prmodecont & 1) ? (int)((p->prim >> 9) & 1) : (int)((w->prmode >> 9) & 1);

                        p->item_open = 1;
                        w->item++;
                        w->item_vertex = 0;
                        if (w->mode == SCAN) {
                            flat_add_item(w->flat, (w->prmodecont & 1) ? p->prim : (p->prim & 7) | (uint32_t)(w->prmode & 0x7F8),
                                          w->tex0[c], (uint32_t)(w->frame[c] & 0x1FF));
                        }
                    }
                    if (w->mode == SCAN) {
                        keep_flat_vertex(w, p, q);
                    } else if ((size_t)w->item < w->flat->nitems) {
                        move_flat_vertex(w, p, q);
                    }
                    w->item_vertex++;
                }
                p->st = NULL;
                p->rgbaq = NULL;
                p->uv = NULL;
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
        } else {
            n = count - i < p->left ? count - i : p->left;
            p->left -= (uint32_t)n;
            keep = !p->dropping;
        }
        if (keep) {
            if (out != i) {
                memmove(data + out * 16, data + i * 16, n * 16);
            }
            out += n;
        }
        i += n;
    }
    return out * 16;
}

/* ---- At a vblank ----------------------------------------------------------------------------
   The display registers of vblank n are those the vblank found, before the game swapped at it: the
   chain drawn after the game's swap at vblank 2k - 1 is kept by vblank 2k (where the frame it swapped
   to is first shown) as the pending frame (age 0), and stays pending through vblank 2k + 1 (age 1)
   when nothing else has been sent since and the same buffer is still shown. The pictures between are
   drawn from it: at vblank 2k after the vblank's own picture (the frame before, as the game has it),
   at 2k + 1 from its start; a picture at phase f of the vblank shows the time (age + f) / 2 of the way
   from the frame shown to the one drawn. */
typedef struct {
    uint8_t *data;
    size_t size;
    int age;          /* vblanks since it was kept */
    int cut;          /* the cameras are a cut apart: no pictures between */
    Walk scan;        /* what the walk found in it */
    uint32_t shown;   /* the buffer shown at the vblank that kept it */
    Camera before;    /* the camera of the frame shown then */
    int have_before;
    const Objects *objects, *objects_before; /* its runs, and the frame's before (NULL: none) */
    Flat *flat;                    /* its 2D items, paired with the frame before's */
    const Flat *flat_before;       /* (NULL: none) */
    int dirty;                     /* a picture between changed a buffer the next frame reads first */
    int reads_prev;                /* the frame blends in the frame shown (Game_BlendPreviousFrame) */
    int snapshot_sent;             /* and the native GS was asked to keep the frame shown */
} Pending;

/* The runs of the last frame drawn and of the one before, and one to walk the next into */
static Objects objects_pool[3];
static Objects *objects_new = &objects_pool[0], *objects_last = &objects_pool[1], *objects_before = &objects_pool[2];
static int objects_last_set, objects_before_set;
/* the same for the 2D items; and the uploads of the frame walked last, and of the pending one */
static Flat flat_pool[3];
static Flat *flat_new = &flat_pool[0], *flat_last = &flat_pool[1], *flat_before = &flat_pool[2];
static Uploads uploads_new_frame;
static Uploads *uploads_new = &uploads_new_frame;

static Pending pending; /* a frame drawn and not yet shown (size 0: none) */
static size_t pending_room;
static Camera last_camera; /* the camera of the last frame drawn */
static int last_camera_set;
static uint8_t *copy;
static size_t copy_room;
static uint64_t stat_replays, stat_cuts, stat_moved, stat_dropped, stat_objects[SECTIONS], stat_items, stat_left_out, stat_restores;
static uint64_t stat_ticks; /* the replays' walks, on this thread */
static uint64_t stat_sent;  /* the bytes they sent */

void port_interp_forget(void) {
    last_camera_set = 0;
    objects_last_set = objects_before_set = 0;
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

static void walk_all(Walk *w, uint8_t *data, size_t size, int mode, Objects *objects, Flat *flat, Uploads *uploads) {
    size_t at;

    memset(w, 0, sizeof(*w));
    w->mode = mode;
    w->prmodecont = 1;
    w->run = -1;
    w->item = -1;
    w->objects = objects;
    w->flat = flat;
    w->uploads = uploads;
    if (objects != NULL) {
        objects_reset(objects);
    }
    if (flat != NULL) {
        flat_reset(flat);
    }
    if (uploads != NULL) {
        uploads_reset(uploads);
    }
    for (at = 0; at < size;) {
        Record r;

        memcpy(&r, data + at, sizeof(r));
        walk_record(w, &r, data + at + sizeof(r));
        at += sizeof(r) + r.bytes;
    }
    if (objects != NULL) {
        objects_finish(objects);
    }
}

/* The pending frame again, into the buffer shown, seen from the camera between (t of the way); with t 1
   and exact, the game's frame as it was (port_interp_end_vblank) */
static void replay_pending(const Camera *between, double t, int exact, void (*send)(int path, const void *data, size_t bytes)) {
    static Walk w; /* (large: kept off the stack) */
    const Walk *scan = &pending.scan;
    uint64_t fix[2 * 9];
    uint64_t start = port_options.stats ? port_ticks() : 0;
    size_t at;
    int n = 0, i;

    memset(&w, 0, sizeof(w));
    w.mode = REPLAY;
    w.prmodecont = 1;
    w.run = -1;
    w.t = interp_still() ? 1.0 : t;
    w.exact = exact;
    w.draw_fbp = scan->draw_fbp;
    w.shown_fbp = pending.shown;
    w.screen = scan->screen;
    w.screen_reflect = scan->screen_reflect;
    w.reflect_fbp = scan->reflect_fbp;
    w.have_reflect = scan->have_reflect && !exact;
    w.camera = scan->camera;
    w.mid = interp_still() || exact ? scan->camera : *between;
    w.objects = interp_still() || exact ? NULL : (Objects *)pending.objects;
    w.prev = pending.objects_before;
    w.item = -1;
    w.flat = interp_still() || exact || pending.flat_before == NULL ? NULL : pending.flat;
    w.copies = exact ? NULL : pending.flat;
    w.flat_prev = pending.flat_before;
    w.ask_uploads = port_gs_replay_uploads();
    {
        /* the native GS's marks around the picture between (it records the game's uploads, not these) */
        uint64_t mark_begin[4] = {1 | ((uint64_t)1 << 15) | ((uint64_t)1 << 60), 0xE, (uint64_t)0xF0 << 32,
                                  PORT_GS_REG_FRAME_INTERP};

        send(3, mark_begin, sizeof(mark_begin));
        if (pending.reads_prev && w.ask_uploads && !exact) {
            /* the frame shown, kept before the first picture between draws over it (the later ones read
               the copy too) */
            if (!pending.snapshot_sent) {
                uint64_t mark_keep[4] = {1 | ((uint64_t)1 << 15) | ((uint64_t)1 << 60), 0xE,
                                         ((uint64_t)0xF2 << 32) | pending.shown, PORT_GS_REG_FRAME_INTERP};

                send(3, mark_keep, sizeof(mark_keep));
                pending.snapshot_sent = 1;
            }
            w.prev_frame_kept = 1;
        }
    }
    for (i = 0; i < 3; i++) {
        w.eye_move[i] = scan->camera.eye[i] - pending.before.eye[i];
    }
    move_between(&scan->camera, &w.mid, &w.move);
    for (at = 0; at < pending.size;) {
        Record r;

        memcpy(&r, pending.data + at, sizeof(r));
        {
            /* a record that is all an upload's data: sent as it is, or left out when the native GS has
               the upload (no copy, no walk) */
            GifWalk *pw = &w.paths[r.what & 0xFF];
            size_t qwords = r.bytes / 16;

            if (pw->flg == 2 && pw->left >= qwords && qwords > 0 && !(r.what & RECORD_3D)) {
                pw->left -= (uint32_t)qwords;
                if (!pw->dropping) {
                    send((int)(r.what & 0xFF), pending.data + at + sizeof(r), r.bytes);
                    stat_sent += r.bytes;
                }
                at += sizeof(r) + r.bytes;
                continue;
            }
        }
        if (copy_room < r.bytes) {
            free(copy);
            copy_room = r.bytes * 2;
            copy = malloc(copy_room);
            if (copy == NULL) {
                PORT_FATAL("out of memory");
            }
        }
        memcpy(copy, pending.data + at + sizeof(r), r.bytes);
        {
            size_t bytes;

            w.record_reads_prev = 0;
            bytes = walk_record(&w, &r, copy);
            if (bytes != 0) {
                /* a record that reads the frame shown before (Game_BlendPreviousFrame's packet is one of
                   its own): between marks that have the native GS read its copy (take_snapshot) */
                uint64_t mark[4] = {1 | ((uint64_t)1 << 15) | ((uint64_t)1 << 60), 0xE, (uint64_t)0xF4 << 32,
                                    PORT_GS_REG_FRAME_INTERP};

                if (w.record_reads_prev) {
                    send(3, mark, sizeof(mark));
                }
                send((int)(r.what & 0xFF), copy, bytes);
                stat_sent += bytes;
                if (w.record_reads_prev) {
                    mark[2] = (uint64_t)0xF5 << 32;
                    send(3, mark, sizeof(mark));
                }
            }
        }
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
    n++;
    fix[2 * n] = (uint64_t)0xF1 << 32; /* the end of the picture between */
    fix[2 * n + 1] = PORT_GS_REG_FRAME_INTERP;
    fix[0] = (uint64_t)n | ((uint64_t)1 << 15) | ((uint64_t)1 << 60); /* NLOOP n, EOP, PACKED, NREG 1 */
    fix[1] = 0xE;                                                       /* A+D */
    send(3, fix, (size_t)(n + 1) * 16);
    stat_replays += !exact;
    stat_restores += exact;
    stat_items += w.items_moved;
    stat_left_out += w.upload_bytes_left_out;
    stat_moved += w.moved;
    stat_dropped += w.dropped;
    for (i = 0; i < SECTIONS; i++) {
        stat_objects[i] += w.objects_moved[i];
    }
    if (port_options.stats) {
        stat_ticks += port_ticks() - start;
    }
}

void port_interp_vsync(const uint64_t *privileged) {
    static Walk w; /* (large: kept off the stack) */
    /* the buffer shown: DISPFB2's, or DISPFB1's when only read circuit 1 is on */
    uint64_t dispfb = (privileged[0] & 2) ? privileged[0x90 / 8] : privileged[0x70 / 8];
    uint32_t shown = (uint32_t)(dispfb & 0x1FF);

    if (pending.size != 0) {
        /* the vblank after the one that kept it: nothing sent since, the same buffer shown */
        if (pending.age == 0 && kept_size == 0 && kept_whole && shown == pending.shown) {
            pending.age = 1;
        } else {
            pending.size = 0;
        }
    }
    if (!kept_whole || kept_size == 0) {
        return;
    }
    walk_all(&w, kept, kept_size, SCAN, objects_new, flat_new, uploads_new);
    if (!w.world_seen) {
        return;
    }
    {
        /* this frame's runs and items become the last frame's */
        Objects *free_one = objects_before;
        Flat *free_flat = flat_before;

        objects_before = objects_last;
        objects_before_set = objects_last_set;
        objects_last = objects_new;
        objects_last_set = 1;
        objects_new = free_one;
        flat_before = flat_last;
        flat_last = flat_new;
        flat_new = free_flat;
        flat_last->draw_fbp = w.draw_fbp;
        flat_last->shown_fbp = shown;
    }
    if (w.draw_fbp != 0xFFFFFFFFu && w.draw_fbp != shown && w.screen.sx != 0 && w.screen.sy != 0) {
        /* kept for the pictures between: the buffers swap, so the packets' becomes the pending one's */
        uint8_t *data = pending.data;
        size_t room = pending_room;

        pending.data = kept;
        pending_room = kept_room;
        pending.size = kept_size;
        pending.age = 0;
        kept = data;
        kept_room = room;
        kept_size = 0;
        pending.scan = w;
        pending.shown = shown;
        pending.before = last_camera;
        pending.have_before = last_camera_set;
        pending.objects = objects_last;
        pending.objects_before = objects_before_set ? objects_before : NULL;
        pending.flat = flat_last;
        pending.flat_before = objects_before_set ? flat_before : NULL;
        pending.dirty = 0;
        objects_pair(objects_last, pending.objects_before ? objects_before : NULL);
        pending.reads_prev = 0;
        pending.snapshot_sent = 0;
        {
            size_t i;

            for (i = 0; i < flat_last->nitems; i++) {
                const Item *it = &flat_last->items[i];

                pending.reads_prev |= (it->prim & 0x10) && it->fbp == w.draw_fbp && (uint32_t)(it->tex & 0x3FFF) == shown * 32;
            }
        }
        flat_pair(flat_last, pending.flat_before, uploads_new);
        flat_copies(flat_last, uploads_new);
        if (w.have_reflect && read_first(uploads_new, w.reflect_fbp)) {
            flat_last->restore = 1; /* (the reflection drawn from the camera between: as for the 2D) */
        }
        {
            Camera between;

            pending.cut = pending.have_before && !camera_between(&pending.before, &w.camera, 0.5, &between);
            stat_cuts += pending.cut;
        }
    }
    last_camera = w.camera;
    last_camera_set = 1;
}

int port_interp_active(void) {
    return pending.size != 0 && pending.have_before && !pending.cut;
}

int port_interp_picture(double phase, void (*send)(int path, const void *data, size_t bytes)) {
    Camera between;
    double t;

    if (!port_interp_active()) {
        return 0;
    }
    t = (pending.age + phase) * 0.5;
    if (t <= 0 || t >= 1) {
        return 0; /* the frame shown, as the game drew it */
    }
    if (!camera_between(&pending.before, &pending.scan.camera, t, &between)) {
        return 0;
    }
    replay_pending(&between, t, 0, send);
    pending.dirty |= pending.flat != NULL && pending.flat->restore && pending.flat_before != NULL;
    return 1;
}

/* After the vblank's pictures between: a buffer other than the frame that a picture between drew
   differently (a scrolling texture's) and that the game's next frame reads before it draws it, is
   drawn again as the game's frame left it: the frame once more, exact, into the buffer shown, which
   the next frame clears. (None of the maps seen so far needs it: they draw those buffers before they
   read them.) */
void port_interp_end_vblank(void (*send)(int path, const void *data, size_t bytes)) {
    if (pending.size != 0 && pending.age == 1 && pending.dirty) {
        replay_pending(&pending.scan.camera, 1.0, 1, send);
        pending.dirty = 0;
    }
}

void port_interp_print_stats(void) {
    fprintf(stderr,
            "[stats] frame rate %d: %llu pictures between drawn (%.3f s walking the packets; %.1f MB of uploads left "
            "out), %llu cuts not, %llu frames drawn again for buffers they changed; %llu vertices moved (with their "
            "model: %llu in the world, %llu of the arm, %llu before the eye), %llu dropped; %llu 2D vertices moved\n",
            port_frame_rate_hz(), (unsigned long long)stat_replays, (double)stat_ticks / port_ticks_per_second(),
            (double)stat_left_out / 1048576.0, (unsigned long long)stat_cuts, (unsigned long long)stat_restores,
            (unsigned long long)stat_moved, (unsigned long long)stat_objects[SECTION_WORLD],
            (unsigned long long)stat_objects[SECTION_ARM], (unsigned long long)stat_objects[SECTION_EYE],
            (unsigned long long)stat_dropped, (unsigned long long)stat_items);
    fprintf(stderr, "[stats] frame rate: the pictures between sent %.1f MB of packets; of %llu model parts placed, %llu "
            "paired by their batch and %llu by their order\n", (double)stat_sent / 1048576.0, (unsigned long long)stat_runs,
            (unsigned long long)stat_runs_key, (unsigned long long)stat_runs_order);
}
