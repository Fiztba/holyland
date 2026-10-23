/* kf4-gsreplay: draws a GS frame capture again (port/debug/gs_capture.c writes them, F9 in the game;
   the format is port/include/port/gs_capture_format.h), with paraLLEl-GS, the GS the game draws with.

     kf4-gsreplay FILE.kf4gs [options]

   The run starts from the capture's GS (VRAM, registers, the scanout's last fields) and VU1 and VIF1,
   and goes through its stream:
   - --mode gif (the default): the GIF packets as the GS had them;
   - --mode vif: the VIF1 stream through the VU1 interpreter (port/platform/vif1.c and vu1.c, built
     in), which makes the PATH1 and PATH2 packets again; each is compared with the recorded one, and the
     vertices of those that differ are listed (--vu-round nearest tests a hypothesis about VU floats).
     It ends with VU1's runs, instructions and cycles for the capture.

   What it can write (tools/README.md has examples):
     --info             the capture's metadata and what is in it
     --frames           each scanout's picture, frame_NNNNNN.png, as --dump-frames writes it
     --out DIR          where the pictures go (the current directory by default)
     --list             every draw: a run of vertices with no GS state written between them, in one
                        packet. Its path, the VU1 program and XGKICK that made it, PRIM, the vertex count,
                        the bounding box (frame buffer pixels), the Z range, and the GS state it drew with
     --draw N, --draws A-B
                        the frame buffer after draw N, or after each of A to B, as draw_NNNNN.png
     --zbuf             with them, the Z buffer too (draw_NNNNN_z.png, grey from the nearest to the
                        farthest Z in the picture)
     --pixel X,Y        the draws whose box covers frame buffer pixel (X,Y), with the pixel's colour and
                        Z after each (--fbp N: only those drawing into frame buffer page N)
     --vertices N       draw N's vertices
     --compare OTHER    the draws of another capture (or of --mode vif against --mode gif of the same
                        one, with --compare-mode), compared one by one: state and vertices
     --vu-round nearest VU1's float arithmetic rounds to nearest instead of toward zero (--mode vif)
     --vu1 NAME         with --mode vif, who runs the VU1 programs (0x000, 0x280, 0x5A0, 0x700;
                        0x276 passes packets through): interp (the interpreter, the default), hle
                        (port/platform/vu1_hle*.c) or check (both, compared run by run: memory,
                        packets, registers, flags and clock)
     --diff N           with --mode vif: show up to N differing vertices of each differing packet (8)
     --renderer NAME    who draws: gs (paraLLEl-GS, the default), native (step 3's native GS,
                        port/gs/native/) or both. With both, --draws writes draw_NNNNN_native.png
                        beside each draw_NNNNN.png, and --frames frame_NNNNNN_native.png
     --compare-renderers
                        both renderers, and after every draw (or each of --draws) the pixels of its box
                        compared: the draws after which they differ, and by how much (up to --limit).
                        After each, the native GS's box is made paraLLEl-GS's, so a draw listed made
                        the difference itself (--no-resync: not, the differences carry on). A pixel
                        differs when a channel is more than 1 apart (--exact: at all)
     --msaa N           the native GS draws the 3D with N samples a pixel (2, 4 or 8: --native-msaa)
     --scale X[,Y]      the native GS's render targets X times the PS2's across and Y down (Y = X if left
                        out): --native-scale, --native-widescreen. --frames and --draws write the larger pictures
     --lod MODE         the native GS's mipmap levels: scale (the default), ps2 or full (--native-lod)
     --cutout-aa MODE   with --msaa, the alpha test's cut-outs: off (the default) or ssaa, shaded at
                        every sample (--native-cutout-aa)
     --colour MODE      the native GS's colour precision: exact (the default) or high (--native-colour)
     --layer-z MODE     the native GS's Z: ps2 (the default) or tolerant (--native-layer-z)
     --nd-oracle        native draw's vertex oracle (nd_oracle.c): every run of VU1 program 0x000 also
                        through nd_000.vert on the GPU and the CPU's plan, compared lane by lane with
                        what VU1 kicked (implies --mode vif; --vu1 hle unless given); and every run of
                        0x280 through nd_280.vert, both passes, and the parser on its blocks
                        (nd_oracle_280.c)
     --native-draw MODE a capture of a run with native draws (--native-draw, docs/port/NATIVE_DRAW.md)
                        holds the draws (records ND_MESH, ND_DRAWS), fed back to the renderer core
                        before the frame's writes that name them. The native GS draws them (native, the
                        default) or, of a capture taken with --native-draw-compare, the PS2 path's
                        packets queued beside them (ps2: --native-draw-compare ps2). paraLLEl-GS always
                        draws the PS2 path's
   Pictures need a Vulkan GPU; --info, --list, --vertices and --compare do not. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port/debug.h"
#include "port/gs_capture_format.h"
#include "port/host.h"
#include "port/lz4_block.h"
#include "port/platform.h"
#include "port/render.h"

int port_gs_backend_open(void);
void *port_gs_backend_context(void);
void port_gs_backend_gif(int path, const void *data, size_t bytes);
void port_gs_backend_read_fifo(void *dst, uint32_t qwords);
int port_gs_backend_vsync(const uint64_t *privileged, int field, int want_pixels, PortGsFrame *frame);
#include "gsn.h"
#include "nd_oracle.h"

/* ---- What vif1.c, vu1.c, video.c and gs_parallel.cpp need of the program --------------------- */
PortOptions port_options;
PortStats port_stats;
int port_gs_capture_recording; /* set in --mode vif, so vif1.c reports each VU1 run */
volatile int port_gs_capture_armed;

static uint8_t vu1_micro_memory[16384];
static uint8_t vu1_data_memory[16384];

void *ee_hw_ptr(uint32_t ee_address) {
    return ee_address == 0x11008000 ? (void *)vu1_micro_memory : (void *)vu1_data_memory;
}

/* vu1.c times its runs with these (the VU1 line of --mode vif) */
uint64_t port_ticks(void) {
    struct timespec t;

#ifdef _WIN32
    timespec_get(&t, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* video.c's picture queue times pictures with this */
double port_wall_seconds(void) {
    return (double)port_ticks() / 1e9;
}

void port_todo(const char *what, const char *file, int line) {
    fprintf(stderr, "todo: %s (%s:%d)\n", what, file, line);
}

void port_fatal(const char *file, int line, const char *fmt, ...) {
    va_list args;

    fprintf(stderr, "kf4-gsreplay: fatal (%s:%d): ", file, line);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

void port_gif_path3_poll(void) {
}

int port_gs_enabled(void) {
    return 1;
}

void port_gs_capture_vif1(const uint32_t *words, size_t count) {
    (void)words;
    (void)count;
}

void port_gs_capture_gif(int path, const void *data, size_t bytes) {
    (void)path;
    (void)data;
    (void)bytes;
}

void port_gs_capture_read_fifo(uint32_t qwords) {
    (void)qwords;
}

static uint32_t current_program = 0xFFFFFFFEu; /* the VU1 program last started (KF4GS_MSCNT: MSCNT) */
static uint32_t current_kick;

void port_gs_capture_vu1_run(uint32_t address, uint32_t top, uint32_t itop) {
    (void)top;
    (void)itop;
    if (address != KF4GS_MSCNT) {
        current_program = address;
    }
    current_kick = 0;
    if (nd_oracle_on()) {
        nd_oracle_vu1_run(current_program, address != KF4GS_MSCNT, top);
    }
}

static void die(const char *fmt, ...) {
    va_list args;

    fprintf(stderr, "kf4-gsreplay: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

/* ---- The file ------------------------------------------------------------------------------------ */
typedef struct {
    uint32_t type;
    uint32_t size;
    const uint8_t *data;
} Record;

typedef struct {
    uint8_t *body;
    size_t body_size;
    Record *records;
    size_t count;
    const char *meta;
} Capture;

static uint64_t fnv(uint64_t hash, const uint8_t *p, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        hash = (hash ^ p[i]) * 0x100000001B3ull;
    }
    return hash;
}

static void load_capture(const char *path, Capture *c) {
    FILE *file = fopen(path, "rb");
    Kf4GsHeader header;
    uint8_t *packed;
    size_t at, room = 0;

    memset(c, 0, sizeof(*c));
    if (file == NULL) {
        die("cannot open %s", path);
    }
    if (fread(&header, sizeof(header), 1, file) != 1 || memcmp(header.magic, KF4GS_MAGIC, 8) != 0) {
        die("%s is not a GS capture", path);
    }
    if (header.version != KF4GS_VERSION) {
        die("%s is a GS capture of version %u; this reads %u", path, header.version, KF4GS_VERSION);
    }
    fseek(file, (long)header.header_size, SEEK_SET);
    c->body = malloc(header.body_size + 1);
    packed = malloc(KF4GS_BLOCK_SIZE);
    if (c->body == NULL || packed == NULL) {
        die("out of memory");
    }
    for (at = 0; at < header.body_size; at += KF4GS_BLOCK_SIZE) {
        uint32_t sizes[2];
        size_t n = header.body_size - at < KF4GS_BLOCK_SIZE ? header.body_size - at : KF4GS_BLOCK_SIZE;

        if (fread(sizes, sizeof(sizes), 1, file) != 1 || sizes[0] != n || sizes[1] > n ||
            fread(packed, 1, sizes[1], file) != sizes[1]) {
            die("%s is cut short or damaged", path);
        }
        if (sizes[1] == n) {
            memcpy(c->body + at, packed, n);
        } else if (!lz_decompress(packed, sizes[1], c->body + at, n)) {
            die("%s is damaged", path);
        }
    }
    free(packed);
    fclose(file);
    c->body_size = header.body_size;
    if (fnv(0xCBF29CE484222325ull, c->body, c->body_size) != header.body_hash) {
        die("%s is damaged (its hash)", path);
    }
    for (at = 0; at + sizeof(Kf4GsRecord) <= c->body_size;) {
        Kf4GsRecord r;

        memcpy(&r, c->body + at, sizeof(r));
        at += sizeof(r);
        if (r.size > c->body_size - at) {
            die("%s: a record runs past the end", path);
        }
        if (c->count == room) {
            room = room ? room * 2 : 4096;
            c->records = realloc(c->records, room * sizeof(Record));
        }
        c->records[c->count].type = r.type;
        c->records[c->count].size = r.size;
        c->records[c->count].data = c->body + at;
        c->count++;
        at += r.size;
        if (r.type == KF4GS_META && c->meta == NULL) {
            char *text = malloc(r.size + 1);

            memcpy(text, c->body + at - r.size, r.size);
            text[r.size] = 0;
            c->meta = text;
        }
        if (r.type == KF4GS_END) {
            break;
        }
    }
    if (c->meta == NULL) {
        c->meta = "";
    }
}

static long long meta_int(const Capture *c, const char *key, long long fallback) {
    size_t n = strlen(key);
    const char *p = c->meta;

    while (p && *p) {
        if (strncmp(p, key, n) == 0 && p[n] == '=') {
            return strtoll(p + n + 1, NULL, 0);
        }
        p = strchr(p, '\n');
        if (p) {
            p++;
        }
    }
    return fallback;
}

/* ---- GS memory: the swizzles of the frame and Z buffer formats ----------------------------------- */
static const uint8_t block32[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
static const uint8_t block32z[4][8] = {
    {24, 25, 28, 29, 8, 9, 12, 13}, {26, 27, 30, 31, 10, 11, 14, 15},
    {16, 17, 20, 21, 0, 1, 4, 5}, {18, 19, 22, 23, 2, 3, 6, 7}};
static const uint8_t column32[8][8] = {
    {0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15},
    {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
    {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
    {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
static const uint8_t block16[8][4] = {
    {0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
    {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
static const uint8_t block16s[8][4] = {
    {0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27},
    {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
static const uint8_t block16z[8][4] = {
    {24, 26, 16, 18}, {25, 27, 17, 19}, {28, 30, 20, 22}, {29, 31, 21, 23},
    {8, 10, 0, 2}, {9, 11, 1, 3}, {12, 14, 4, 6}, {13, 15, 5, 7}};
static const uint8_t block16sz[8][4] = {
    {24, 26, 8, 10}, {25, 27, 9, 11}, {16, 18, 0, 2}, {17, 19, 1, 3},
    {28, 30, 12, 14}, {29, 31, 13, 15}, {20, 22, 4, 6}, {21, 23, 5, 7}};
static const uint8_t column16[8][16] = {
    {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
    {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
    {32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59},
    {36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63},
    {64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91},
    {68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95},
    {96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123},
    {100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127}};

static int psm_is_16(uint32_t psm) {
    return psm == 0x02 || psm == 0x0A || psm == 0x32 || psm == 0x3A;
}

/* A pixel of a buffer at page `fbp` (2048 words each), `fbw` 64-pixel columns wide, in format psm */
static uint32_t vram_pixel(const uint8_t *vram, uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t x, uint32_t y) {
    if (fbw == 0) {
        fbw = 1;
    }
    if (psm_is_16(psm)) {
        const uint8_t(*blocks)[4] = psm == 0x02 ? block16 : psm == 0x0A ? block16s : psm == 0x32 ? block16z : block16sz;
        uint32_t page = fbp + (y / 64) * fbw + x / 64;
        uint32_t half = page * 4096 + blocks[(y % 64) / 8][(x % 64) / 16] * 128 + column16[y % 8][x % 16];
        uint16_t v;

        memcpy(&v, vram + ((size_t)half * 2 & 0x3FFFFF), 2);
        return v;
    } else {
        const uint8_t(*blocks)[8] = psm >= 0x30 ? block32z : block32;
        uint32_t page = fbp + (y / 32) * fbw + x / 64;
        uint32_t word = page * 2048 + blocks[(y % 32) / 8][(x % 64) / 8] * 64 + column32[y % 8][x % 8];
        uint32_t v;

        memcpy(&v, vram + ((size_t)word * 4 & 0x3FFFFF), 4);
        return (psm & 0xF) == 1 ? v & 0xFFFFFF : v; /* CT24, Z24 */
    }
}

static uint32_t to_rgba(uint32_t v, uint32_t psm) {
    if (psm_is_16(psm)) {
        uint32_t r = (v & 0x1F) << 3, g = ((v >> 5) & 0x1F) << 3, b = ((v >> 10) & 0x1F) << 3;

        return r | (g << 8) | (b << 16) | 0xFF000000u;
    }
    return (v & 0xFFFFFF) | 0xFF000000u;
}

/* ---- The GIF, read as it goes to the GS: the draws ----------------------------------------------- */
typedef struct {
    uint16_t x, y; /* 12.4, before XYOFFSET */
    uint32_t z;
    uint32_t rgbaq; /* R G B A */
    uint32_t q;
    uint32_t s, t; /* float bits */
    uint16_t u, v;
    uint8_t fog;
    uint8_t kick; /* XYZ2 or XYZF2: a drawing kick (else XYZ3, or ADC) */
} Vertex;

typedef struct {
    uint32_t record, path, program, kick;
    uint32_t prim; /* PRIM as the draw used it (PRMODE's attributes when PRMODECONT's AC is 0) */
    uint32_t vertices, kicks;
    uint32_t first_vertex;
    int32_t box[4]; /* x0 y0 x1 y1: frame buffer pixels */
    uint32_t zmin, zmax;
    uint64_t tex0, tex1, clamp, test, zbuf, alpha, fba, frame, scissor, xyoffset;
    uint64_t pabe, colclamp, texa, fogcol, dthe, dimx, texclut;
    uint32_t end_qword; /* its last vertex is in the qword before this, in record end_record */
    uint32_t end_record;
    int differs;        /* --mode vif: made from a packet that differs from the recorded one */
} Draw;

typedef struct {
    uint32_t left, flg, nreg, reg, eop, in_packet, dwords;
    uint64_t regs;
} GifPathState;

typedef struct {
    GifPathState paths[4];
    uint64_t reg[256];  /* the GS's registers as last written */
    uint32_t internal_q;
    Draw *draws;
    size_t count, room;
    Vertex *vertices;
    size_t vertex_count, vertex_room;
    int open;           /* draws[count - 1] takes vertices */
    uint32_t record;    /* the record and qword being read */
    uint32_t qword;
    uint32_t path;
    int differs;
    /* a draw closed: its index (-1: none) */
    long closed;
} Gif;

static void gif_init(Gif *g, const Capture *c) {
    size_t i;

    memset(g, 0, sizeof(*g));
    g->closed = -1;
    for (i = 0; i < c->count; i++) {
        if (c->records[i].type == KF4GS_GS_REGS) {
            const Kf4GsReg *regs = (const Kf4GsReg *)c->records[i].data;
            size_t n;

            for (n = 0; n < c->records[i].size / sizeof(Kf4GsReg); n++) {
                if (regs[n].address < 256) {
                    g->reg[regs[n].address] = regs[n].value;
                } else if (regs[n].address == KF4GS_REG_INTERNAL_Q) {
                    g->internal_q = (uint32_t)regs[n].value;
                }
            }
        }
    }
}

static void close_draw(Gif *g) {
    if (g->open) {
        g->open = 0;
        g->closed = (long)g->count - 1;
    }
}

static uint32_t effective_prim(const Gif *g) {
    uint32_t prim = (uint32_t)g->reg[0x00] & 0x7FF;

    if (!(g->reg[0x1A] & 1)) {
        prim = (prim & 7) | ((uint32_t)g->reg[0x1B] & 0x7F8);
    }
    return prim;
}

static void open_draw(Gif *g) {
    Draw *d;
    uint32_t ctx;

    if (g->count == g->room) {
        g->room = g->room ? g->room * 2 : 1024;
        g->draws = realloc(g->draws, g->room * sizeof(Draw));
    }
    d = &g->draws[g->count++];
    memset(d, 0, sizeof(*d));
    d->record = g->record;
    d->path = g->path;
    d->program = g->path == 1 ? current_program : 0xFFFFFFFEu;
    d->kick = g->path == 1 ? current_kick : 0;
    d->prim = effective_prim(g);
    ctx = (d->prim >> 9) & 1;
    d->first_vertex = (uint32_t)g->vertex_count;
    d->box[0] = d->box[1] = 0x7FFFFFFF;
    d->box[2] = d->box[3] = -0x7FFFFFFF;
    d->zmin = 0xFFFFFFFFu;
    d->tex0 = g->reg[0x06 + ctx];
    d->tex1 = g->reg[0x14 + ctx];
    d->clamp = g->reg[0x08 + ctx];
    d->xyoffset = g->reg[0x18 + ctx];
    d->scissor = g->reg[0x40 + ctx];
    d->alpha = g->reg[0x42 + ctx];
    d->test = g->reg[0x47 + ctx];
    d->fba = g->reg[0x4A + ctx];
    d->frame = g->reg[0x4C + ctx];
    d->zbuf = g->reg[0x4E + ctx];
    d->pabe = g->reg[0x49];
    d->colclamp = g->reg[0x46];
    d->texa = g->reg[0x3B];
    d->fogcol = g->reg[0x3D];
    d->dthe = g->reg[0x45];
    d->dimx = g->reg[0x44];
    d->texclut = g->reg[0x1C];
    d->differs = g->differs;
    g->open = 1;
}

static void add_vertex(Gif *g, uint32_t address, uint64_t value) {
    Draw *d;
    Vertex *v;
    int fog = address == 0x04 || address == 0x0C;
    int32_t px, py;

    if (!g->open) {
        open_draw(g);
    }
    d = &g->draws[g->count - 1];
    if (g->vertex_count == g->vertex_room) {
        g->vertex_room = g->vertex_room ? g->vertex_room * 2 : 65536;
        g->vertices = realloc(g->vertices, g->vertex_room * sizeof(Vertex));
    }
    v = &g->vertices[g->vertex_count++];
    v->x = (uint16_t)value;
    v->y = (uint16_t)(value >> 16);
    v->z = fog ? (uint32_t)(value >> 32) & 0xFFFFFF : (uint32_t)(value >> 32);
    v->fog = fog ? (uint8_t)(value >> 56) : (uint8_t)(g->reg[0x0A] >> 56);
    v->rgbaq = (uint32_t)g->reg[0x01];
    v->q = (uint32_t)(g->reg[0x01] >> 32);
    v->s = (uint32_t)g->reg[0x02];
    v->t = (uint32_t)(g->reg[0x02] >> 32);
    v->u = (uint16_t)(g->reg[0x03] & 0x3FFF);
    v->v = (uint16_t)((g->reg[0x03] >> 16) & 0x3FFF);
    v->kick = address == 0x04 || address == 0x05;
    d->vertices++;
    d->kicks += v->kick;
    px = ((int32_t)v->x - (int32_t)(d->xyoffset & 0xFFFF)) / 16;
    py = ((int32_t)v->y - (int32_t)((d->xyoffset >> 32) & 0xFFFF)) / 16;
    if (px < d->box[0]) d->box[0] = px;
    if (py < d->box[1]) d->box[1] = py;
    if (px > d->box[2]) d->box[2] = px;
    if (py > d->box[3]) d->box[3] = py;
    if (v->z < d->zmin) d->zmin = v->z;
    if (v->z > d->zmax) d->zmax = v->z;
    d->end_record = g->record;
    d->end_qword = g->qword + 1;
}

/* A register write, as the GS takes it */
static void gs_write(Gif *g, uint32_t address, uint64_t value) {
    switch (address) {
    case 0x04: case 0x05: case 0x0C: case 0x0D:
        add_vertex(g, address, value);
        return;
    case 0x01:
        g->reg[0x01] = value;
        return;
    case 0x02: case 0x03: case 0x0A:
        g->reg[address] = value;
        return;
    case 0x0F: case 0x7E: /* NOP, the port's widescreen mark */
        return;
    }
    close_draw(g);
    g->reg[address & 0xFF] = value;
}

/* A PACKED qword written by descriptor `desc`, as its register's value */
static void packed(Gif *g, uint32_t desc, const uint64_t q[2]) {
    switch (desc) {
    case 0x0:
        gs_write(g, 0x00, q[0] & 0x7FF);
        break;
    case 0x1:
        gs_write(g, 0x01, (q[0] & 0xFF) | ((q[0] >> 24) & 0xFF00) | ((q[1] & 0xFF) << 16) |
                              ((q[1] >> 8) & 0xFF000000u) | ((uint64_t)g->internal_q << 32));
        break;
    case 0x2:
        g->internal_q = (uint32_t)q[1];
        gs_write(g, 0x02, q[0]);
        break;
    case 0x3:
        gs_write(g, 0x03, (q[0] & 0x3FFF) | (((q[0] >> 32) & 0x3FFF) << 16));
        break;
    case 0x4:
    case 0xC: {
        uint64_t v = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | (((q[1] >> 4) & 0xFFFFFF) << 32) |
                     (((q[1] >> 36) & 0xFF) << 56);
        gs_write(g, desc == 0x4 && !((q[1] >> 47) & 1) ? 0x04 : 0x0C, v);
        break;
    }
    case 0x5:
    case 0xD: {
        uint64_t v = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | ((q[1] & 0xFFFFFFFFu) << 32);
        gs_write(g, desc == 0x5 && !((q[1] >> 47) & 1) ? 0x05 : 0x0D, v);
        break;
    }
    case 0xA:
        gs_write(g, 0x0A, ((q[1] >> 36) & 0xFF) << 56);
        break;
    case 0xE:
        gs_write(g, (uint32_t)(q[1] & 0xFF), q[0]);
        break;
    case 0xF:
        break;
    default: /* TEX0, CLAMP: the register's own value */
        gs_write(g, desc, q[0]);
        break;
    }
}

/* One qword of path's stream */
static void gif_qword(Gif *g, uint32_t path, const uint64_t q[2]) {
    GifPathState *p = &g->paths[path];

    g->path = path;
    if (p->left == 0) {
        uint64_t tag = q[0];
        uint32_t nloop = (uint32_t)(tag & 0x7FFF);

        if (!p->in_packet) {
            close_draw(g); /* a draw is never longer than a packet */
            if (path == 1) {
                current_kick++;
            }
        }
        p->eop = (uint32_t)((tag >> 15) & 1);
        p->flg = (uint32_t)((tag >> 58) & 3);
        p->nreg = (uint32_t)(tag >> 60);
        if (p->nreg == 0) {
            p->nreg = 16;
        }
        p->regs = q[1];
        p->reg = 0;
        if (p->flg == 0) {
            p->left = nloop * p->nreg;
            if ((tag >> 46) & 1) {
                gs_write(g, 0x00, (tag >> 47) & 0x7FF);
            }
        } else if (p->flg == 1) {
            p->left = (nloop * p->nreg + 1) / 2;
            p->dwords = nloop * p->nreg;
        } else {
            p->left = nloop;
        }
        p->in_packet = !(p->eop && p->left == 0);
        g->internal_q = 0x3F800000u; /* a tag sets Q to 1 */
    } else {
        if (p->flg == 0) {
            packed(g, (uint32_t)(p->regs >> (p->reg * 4)) & 0xF, q);
            p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
        } else if (p->flg == 1) {
            int half;

            for (half = 0; half < 2 && p->dwords; half++, p->dwords--) {
                uint32_t desc = (uint32_t)(p->regs >> (p->reg * 4)) & 0xF;

                if (desc < 0xE) {
                    gs_write(g, desc, q[half]);
                }
                p->reg = p->reg + 1 == p->nreg ? 0 : p->reg + 1;
            }
        }
        p->left--;
        if (p->left == 0 && p->eop) {
            p->in_packet = 0;
        }
    }
    if (!p->in_packet && p->left == 0) {
        close_draw(g);
    }
}

/* ---- The run ------------------------------------------------------------------------------------- */
enum { MODE_GIF, MODE_VIF };

enum { RENDER_GS = 1, RENDER_NATIVE = 2 };

typedef struct {
    const Capture *capture;
    int mode;
    int render;           /* the renderers that draw: RENDER_GS, RENDER_NATIVE */
    int compare_renderers; /* --compare-renderers */
    int resync;            /* (not --no-resync) after each draw, the native GS takes paraLLEl-GS's pixels */
    uint32_t threshold;    /* a pixel differs when a channel is more than this apart (1; --exact: 0) */
    uint64_t last_priv[0xF0 / 8]; /* the display registers at the last vblank */
    uint64_t last_vblank;
    int seen_vsync;
    int limit;
    uint64_t compared, compared_differ;
    Gif gif;
    size_t cursor;        /* --mode vif: the next record not yet matched */
    /* --mode vif's comparison */
    uint64_t packets_same, packets_differ, packets_missing, packets_extra;
    int diff_vertices;
    /* what to do at the end of a draw */
    long draw_from, draw_to; /* --draw / --draws (-1: none) */
    int zbuf;
    int scaled; /* --scale: --draws writes the native GS's targets at their own pixels */
    int pixel_x, pixel_y, pixel; /* --pixel */
    long fbp_filter;
    const char *out;
    int frames;
    PortGsFrame frame;
    PortGsFrame native_frame; /* --compare-renderers: the native GS's picture of a vblank */
    uint32_t scanouts, scanouts_differ, scanout_worst; /* and how the pictures compared */
    uint32_t last_pixel_colour, last_pixel_z;
    int pixel_seen;
} Run;

static void write_png(const Run *r, const char *name, const uint32_t *rgba, uint32_t w, uint32_t h) {
    char path[4096];

    snprintf(path, sizeof(path), "%s/%s", r->out ? r->out : ".", name);
    if (port_png_write(path, rgba, w, h)) {
        fprintf(stderr, "wrote %s (%ux%u)\n", path, w, h);
    }
}

/* What a draw drew into, from the native GS: its frame buffer (and the box) */
static GsnRect draw_rect(const Draw *d, int box_only) {
    GsnRect rect;
    uint32_t fbw = (uint32_t)((d->frame >> 16) & 0x3F);
    uint32_t h = (uint32_t)((d->scissor >> 48) & 0x7FF) + 1;

    rect.bp = (uint32_t)(d->frame & 0x1FF) * 32;
    rect.bw = fbw;
    rect.psm = (uint32_t)((d->frame >> 24) & 0x3F);
    rect.x = rect.y = 0;
    rect.w = fbw ? fbw * 64 : 640;
    rect.h = h > 2048 ? 2048 : h;
    if (box_only) {
        int x0 = d->box[0] < 0 ? 0 : d->box[0], y0 = d->box[1] < 0 ? 0 : d->box[1];
        int x1 = d->box[2] + 1 > (int)rect.w ? (int)rect.w : d->box[2] + 1;
        int y1 = d->box[3] + 1 > (int)rect.h ? (int)rect.h : d->box[3] + 1;

        rect.x = (uint32_t)x0;
        rect.y = (uint32_t)y0;
        rect.w = x1 > x0 ? (uint32_t)(x1 - x0) : 0;
        rect.h = y1 > y0 ? (uint32_t)(y1 - y0) : 0;
    }
    return rect;
}

static void write_png(const Run *r, const char *name, const uint32_t *rgba, uint32_t w, uint32_t h);

/* --compare-renderers: the draw's box as each renderer has it after the draw */
static void compare_draw(Run *r, long index) {
    const Draw *d = &r->gif.draws[index];
    GsnRect rect = draw_rect(d, 1);
    uint32_t *a, *b, max = 0, pixels = 0;
    size_t i, n = (size_t)rect.w * rect.h;

    if (n == 0) {
        return; /* off the frame buffer */
    }
    a = malloc(n * 4);
    b = malloc(n * 4);
    port_gs_backend_read_rect(rect.bp, rect.bw, rect.psm, rect.x, rect.y, rect.w, rect.h, a);
    gsn_read_rect(&rect, b);
    for (i = 0; i < n; i++) {
        uint32_t worst = 0;
        int c;

        for (c = 0; c < 24; c += 8) {
            int e = (int)((a[i] >> c) & 0xFF) - (int)((b[i] >> c) & 0xFF);
            uint32_t m = (uint32_t)(e < 0 ? -e : e);

            worst = m > worst ? m : worst;
        }
        max = worst > max ? worst : max;
        pixels += worst > r->threshold;
    }
    r->compared++;
    if (r->resync) {
        /* the next draw starts from the same picture: what it shows is its own difference */
        port_gs_backend_read_rect(rect.bp, rect.bw, rect.psm, rect.x, rect.y, rect.w, rect.h, a);
        gsn_write_rect(&rect, a);
    }
    if (pixels) {
        r->compared_differ++;
        if (r->compared_differ <= (uint64_t)r->limit) {
            printf("draw %6ld differs: %u of %zu pixels of its box (%u,%u %ux%u) by more than %u, at most %u\n", index,
                   pixels, n, rect.x, rect.y, rect.w, rect.h, r->threshold, max);
        }
    }
    free(a);
    free(b);
}

static void dump_draw_buffers(Run *r, long index) {
    const Draw *d = &r->gif.draws[index];
    const uint8_t *vram;

    if (r->render & RENDER_NATIVE) {
        GsnRect rect = draw_rect(d, 0);
        uint32_t *rgba = malloc((size_t)rect.w * rect.h * 4);
        char name[64];

        uint32_t scale[2] = {1, 1};

        if (r->scaled) {
            /* (--scale: the native GS's target at its own pixels) */
            rgba = realloc(rgba, (size_t)rect.w * rect.h * 4 * GSN_MAX_SCALE_X * 4);
            gsn_read_rect_scaled(&rect, rgba, scale);
        } else {
            gsn_read_rect(&rect, rgba);
        }
        snprintf(name, sizeof(name), (r->render & RENDER_GS) ? "draw_%05ld_native.png" : "draw_%05ld.png", index);
        write_png(r, name, rgba, rect.w * scale[0], rect.h * scale[1]);
        free(rgba);
    }
    if (!(r->render & RENDER_GS)) {
        return;
    }
    vram = port_gs_backend_vram();
    uint32_t fbp = (uint32_t)(d->frame & 0x1FF) * 1, fbw = (uint32_t)((d->frame >> 16) & 0x3F);
    uint32_t psm = (uint32_t)((d->frame >> 24) & 0x3F);
    uint32_t w = fbw * 64, h = (uint32_t)((d->scissor >> 48) & 0x7FF) + 1;
    uint32_t x, y, *rgba;
    char name[64];

    if (w == 0 || w > 2048) {
        w = 640;
    }
    if (h > 2048) {
        h = 2048;
    }
    rgba = malloc(sizeof(uint32_t) * w * h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            rgba[y * w + x] = to_rgba(vram_pixel(vram, fbp, fbw, psm, x, y), psm);
        }
    }
    snprintf(name, sizeof(name), "draw_%05ld.png", index);
    write_png(r, name, rgba, w, h);
    if (r->zbuf) {
        uint32_t zbp = (uint32_t)(d->zbuf & 0x1FF), zpsm = 0x30 | (uint32_t)((d->zbuf >> 24) & 0xF);
        uint32_t lo = 0xFFFFFFFFu, hi = 0, *z = malloc(sizeof(uint32_t) * w * h);

        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint32_t v = vram_pixel(vram, zbp, fbw, zpsm, x, y);

                z[y * w + x] = v;
                if (v != 0 && v < lo) lo = v;
                if (v > hi) hi = v;
            }
        }
        for (y = 0; y < w * h; y++) {
            uint32_t v = z[y];
            uint32_t g = v == 0 || hi <= lo ? 0 : (uint32_t)(32 + (double)(v - lo) * 223.0 / (double)(hi - lo));

            z[y] = g | (g << 8) | (g << 16) | 0xFF000000u;
        }
        snprintf(name, sizeof(name), "draw_%05ld_z.png", index);
        write_png(r, name, z, w, h);
        free(z);
    }
    free(rgba);
}

static int box_covers(const Draw *d, int x, int y) {
    int sx0 = (int)(d->scissor & 0x7FF), sx1 = (int)((d->scissor >> 16) & 0x7FF);
    int sy0 = (int)((d->scissor >> 32) & 0x7FF), sy1 = (int)((d->scissor >> 48) & 0x7FF);

    return x >= d->box[0] && x <= d->box[2] && y >= d->box[1] && y <= d->box[3] && x >= sx0 && x <= sx1 &&
           y >= sy0 && y <= sy1;
}

static void pixel_after(Run *r, long index) {
    const Draw *d = &r->gif.draws[index];
    const uint8_t *vram;
    uint32_t fbw = (uint32_t)((d->frame >> 16) & 0x3F), psm = (uint32_t)((d->frame >> 24) & 0x3F);
    uint32_t colour, z;

    if (!box_covers(d, r->pixel_x, r->pixel_y) || (r->fbp_filter >= 0 && (long)(d->frame & 0x1FF) != r->fbp_filter)) {
        return;
    }
    if (!(r->render & RENDER_GS)) {
        printf("draw %ld covers (%d,%d) by its box: FBP %u ZBP %u TEST %08llX\n", index, r->pixel_x, r->pixel_y,
               (unsigned)(d->frame & 0x1FF), (unsigned)(d->zbuf & 0x1FF), (unsigned long long)d->test);
        return;
    }
    vram = port_gs_backend_vram();
    colour = vram_pixel(vram, (uint32_t)(d->frame & 0x1FF), fbw, psm, (uint32_t)r->pixel_x, (uint32_t)r->pixel_y);
    z = vram_pixel(vram, (uint32_t)(d->zbuf & 0x1FF), fbw, 0x30 | (uint32_t)((d->zbuf >> 24) & 0xF),
                   (uint32_t)r->pixel_x, (uint32_t)r->pixel_y);
    printf("draw %6ld path %u prog %03X kick %3u  FBP %3u colour %08X Z %08X%s  zrange %08X-%08X TEST %05llX "
           "ZBUF %09llX ALPHA %010llX PRIM %03X\n",
           index, d->path, d->program & 0xFFF, d->kick, (unsigned)(d->frame & 0x1FF), colour, z,
           r->pixel_seen && (colour != r->last_pixel_colour || z != r->last_pixel_z) ? " changed" : "", d->zmin,
           d->zmax, (unsigned long long)d->test, (unsigned long long)d->zbuf, (unsigned long long)d->alpha,
           d->prim);
    r->last_pixel_colour = colour;
    r->last_pixel_z = z;
    r->pixel_seen = 1;
}

static int wants_draw_end(const Run *r) {
    return r->draw_from >= 0 || r->pixel || r->compare_renderers;
}

static void draw_ended(Run *r, long index) {
    int in_range = r->draw_from < 0 || (index >= r->draw_from && index <= r->draw_to);

    if (r->draw_from >= 0 && in_range && r->render && !r->compare_renderers) {
        dump_draw_buffers(r, index);
    }
    if (r->compare_renderers && in_range) {
        if (r->draw_from >= 0) {
            dump_draw_buffers(r, index); /* (before the native GS is put back in step) */
        }
        compare_draw(r, index);
    }
    if (r->pixel) {
        pixel_after(r, index);
    }
}

/* The renderers, as the run has them */
static void gs_gif(Run *r, int path, const void *data, size_t bytes) {
    if (r->render & RENDER_GS) {
        port_gs_backend_gif(path, data, bytes);
    }
    if (r->render & RENDER_NATIVE) {
        gsn_gif(path, data, bytes);
    }
}

/* Hands a transfer to the GS, read as it goes; split after each draw that something waits for */
static void send(Run *r, uint32_t record, int path, const void *data, size_t bytes) {
    const uint64_t *q = data;
    size_t n = bytes / 16, k, start = 0;

    r->gif.record = record;
    for (k = 0; k < n; k++) {
        r->gif.qword = (uint32_t)k;
        gif_qword(&r->gif, (uint32_t)path, q + k * 2);
        if (r->gif.closed >= 0) {
            long index = r->gif.closed;

            r->gif.closed = -1;
            if (wants_draw_end(r)) {
                const Draw *d = &r->gif.draws[index];
                size_t end = d->end_record == record ? d->end_qword : start;

                if (end > start && r->render) {
                    gs_gif(r, path, (const uint8_t *)data + start * 16, (end - start) * 16);
                    start = end;
                }
                draw_ended(r, index);
            }
        }
    }
    if (n > start && r->render) {
        gs_gif(r, path, (const uint8_t *)data + start * 16, (n - start) * 16);
    }
}

/* --compare-renderers: the buffer the display shows (by the display registers `priv`), as each renderer
   has it now */
static void compare_display(Run *r, const uint64_t *priv, const char *when, uint64_t vblank) {
    GsnRect rect;

    if (gsn_display_rect(priv, &rect)) {
            size_t i, n = (size_t)rect.w * rect.h;
            uint32_t *a = malloc(n * 4), *b = malloc(n * 4), max = 0, pixels = 0;

            port_gs_backend_read_rect(rect.bp, rect.bw, rect.psm, rect.x, rect.y, rect.w, rect.h, a);
            gsn_read_rect(&rect, b);
            for (i = 0; i < n; i++) {
                uint32_t worst = 0;
                int c;

                for (c = 0; c < 24; c += 8) {
                    int e = (int)((a[i] >> c) & 0xFF) - (int)((b[i] >> c) & 0xFF);
                    uint32_t m = (uint32_t)(e < 0 ? -e : e);

                    worst = m > worst ? m : worst;
                }
                max = worst > max ? worst : max;
                pixels += worst > r->threshold;
            }
            printf("%s %llu: the displayed buffer (FBP %u) differs in %u of %zu pixels by more than %u, at most %u\n",
                   when, (unsigned long long)vblank, rect.bp / 32, pixels, n, r->threshold, max);
            if (r->frames) {
                char name[64];

                snprintf(name, sizeof(name), "%s_%06llu_gs.png", when, (unsigned long long)vblank);
                write_png(r, name, a, rect.w, rect.h);
                snprintf(name, sizeof(name), "%s_%06llu_native.png", when, (unsigned long long)vblank);
                write_png(r, name, b, rect.w, rect.h);
            }
            free(a);
            free(b);
    }
}

/* --compare-renderers: the pictures of a vblank (the scanouts: each renderer's read of the display,
   field by field or whole, deinterlaced) */
static void compare_scanouts(Run *r, const PortGsFrame *a, const PortGsFrame *b, uint64_t vblank) {
    size_t i, n = (size_t)a->width * a->height;
    uint32_t max = 0, pixels = 0;

    if (a->width != b->width || a->height != b->height) {
        printf("scanout %llu: the pictures are %ux%u and %ux%u\n", (unsigned long long)vblank, a->width, a->height,
               b->width, b->height);
        r->scanouts++;
        r->scanouts_differ++;
        return;
    }
    for (i = 0; i < n; i++) {
        uint32_t worst = 0;
        int c;

        for (c = 0; c < 24; c += 8) {
            int e = (int)((a->rgba[i] >> c) & 0xFF) - (int)((b->rgba[i] >> c) & 0xFF);
            uint32_t m = (uint32_t)(e < 0 ? -e : e);

            worst = m > worst ? m : worst;
        }
        max = worst > max ? worst : max;
        pixels += worst > r->threshold;
    }
    printf("scanout %llu: the picture (%ux%u) differs in %u of %zu pixels by more than %u, at most %u\n",
           (unsigned long long)vblank, a->width, a->height, pixels, n, r->threshold, max);
    r->scanouts++;
    r->scanouts_differ += pixels != 0;
    r->scanout_worst = max > r->scanout_worst ? max : r->scanout_worst;
}

static void vsync(Run *r, const Record *rec) {
    uint64_t vblank;
    uint32_t field;
    int want, got_gs = 0;

    memcpy(&vblank, rec->data, 8);
    memcpy(&field, rec->data + 8, 4);
    if (!r->render) {
        return;
    }
    memcpy(r->last_priv, rec->data + 16, sizeof(r->last_priv));
    r->last_vblank = vblank;
    r->seen_vsync = 1;
    if (r->compare_renderers) {
        compare_display(r, r->last_priv, "vblank", vblank);
    }
    want = r->frames || r->compare_renderers;
    if ((r->render & RENDER_GS) && port_gs_backend_vsync((const uint64_t *)(rec->data + 16), (int)field, want, &r->frame)) {
        got_gs = 1;
        if (r->frames) {
            port_video_dump(&r->frame, vblank);
        }
    }
    if ((r->render & RENDER_NATIVE) &&
        gsn_vsync((const uint64_t *)(rec->data + 16), (int)field, want, &r->native_frame) && want) {
        char name[64];

        if (r->frames) {
            snprintf(name, sizeof(name), (r->render & RENDER_GS) ? "frame_%06llu_native.png" : "frame_%06llu.png",
                     (unsigned long long)vblank);
            port_video_dump_named(&r->native_frame, name);
        }
        if (r->compare_renderers && got_gs) {
            compare_scanouts(r, &r->frame, &r->native_frame, vblank);
        }
    }
}

static void read_fifo(Run *r, const Record *rec) {
    uint32_t qwords;
    void *sink;

    memcpy(&qwords, rec->data, 4);
    if (!r->render) {
        return;
    }
    sink = malloc((size_t)qwords * 16 + 16);
    if (r->render & RENDER_GS) {
        port_gs_backend_read_fifo(sink, qwords);
    }
    if (r->render & RENDER_NATIVE) {
        gsn_read_fifo(sink, qwords);
    }
    free(sink);
}

static Run *vif_run; /* --mode vif: port_gs_gif's run */

/* the vertices of a whole packet, read alone (for --mode vif's differences) */
static size_t packet_vertices(const void *data, size_t bytes, Vertex *out, size_t room) {
    static Gif scratch;
    size_t n = bytes / 16, k, count;

    memset(&scratch, 0, sizeof(scratch));
    scratch.closed = -1;
    for (k = 0; k < n; k++) {
        gif_qword(&scratch, 1, (const uint64_t *)data + k * 2);
    }
    count = scratch.vertex_count < room ? scratch.vertex_count : room;
    memcpy(out, scratch.vertices, count * sizeof(Vertex));
    free(scratch.vertices);
    free(scratch.draws);
    return scratch.vertex_count;
}

static void print_vertex(const char *what, const Vertex *v) {
    float s, t, q;

    memcpy(&s, &v->s, 4);
    memcpy(&t, &v->t, 4);
    memcpy(&q, &v->q, 4);
    printf("      %s x %7.2f y %7.2f z %08X rgba %08X stq %.9g %.9g %.9g uv %u %u fog %u %s\n", what, v->x / 16.0,
           v->y / 16.0, v->z, v->rgbaq, s, t, q, v->u, v->v, v->fog, v->kick ? "kick" : "no kick");
}

static void report_difference(Run *r, uint32_t path, const void *made, size_t made_bytes, const Record *rec) {
    static Vertex a[8192], b[8192];
    size_t na, nb, i, shown = 0;

    r->packets_differ++;
    if (r->diff_vertices <= 0) {
        return;
    }
    na = packet_vertices(rec->data + 4, rec->size - 4, a, 8192);
    nb = packet_vertices(made, made_bytes, b, 8192);
    printf("  packet (record %zu, path %u, program %03X kick %u) differs: %zu recorded vertices, %zu made\n",
           (size_t)(rec - r->capture->records), path, current_program & 0xFFF, current_kick, na, nb);
    for (i = 0; i < na && i < nb && i < 8192 && shown < (size_t)r->diff_vertices; i++) {
        if (memcmp(&a[i], &b[i], sizeof(Vertex)) != 0) {
            printf("    vertex %zu:\n", i);
            print_vertex("recorded", &a[i]);
            print_vertex("made    ", &b[i]);
            shown++;
        }
    }
}

/* --mode vif: VIF1 and VU1 hand their packets here (vif1.c) */
static void handle(Run *r, size_t index);

void port_gs_gif(int path, const void *data, size_t bytes) {
    Run *r = vif_run;
    const Record *recs = r->capture->records;
    int matched = 0;
    uint64_t start = port_ticks(); /* the time here is not VU1's (vu1.c leaves it out) */

    if (nd_oracle_on()) {
        nd_oracle_gif(path, data, bytes);
    }
    /* what was recorded before this packet: PATH3, read-backs (VU1_RUN is this run's own) */
    while (r->cursor < r->capture->count) {
        const Record *rec = &recs[r->cursor];
        uint32_t p;

        if (rec->type == KF4GS_VIFDATA || rec->type == KF4GS_VSYNC || rec->type == KF4GS_END) {
            break; /* this feed made more packets than were recorded */
        }
        if (rec->type == KF4GS_GIF) {
            memcpy(&p, rec->data, 4);
            if (p != 3) {
                if ((int)p == path && rec->size - 4 == ((bytes + 3) & ~(size_t)3) &&
                    memcmp(rec->data + 4, data, bytes) == 0) {
                    r->packets_same++;
                } else {
                    report_difference(r, (uint32_t)path, data, bytes, rec);
                    r->gif.differs = 1;
                }
                matched = 1;
                r->cursor++;
                break;
            }
        }
        handle(r, r->cursor++);
    }
    if (!matched) {
        r->packets_extra++;
    }
    send(r, (uint32_t)(r->cursor ? r->cursor - 1 : 0), path, data, bytes);
    r->gif.differs = 0;
    port_stats.gif_ticks += port_ticks() - start;
}

static void handle(Run *r, size_t index) {
    const Record *rec = &r->capture->records[index];
    uint32_t path;

    switch (rec->type) {
    case KF4GS_GIF:
        memcpy(&path, rec->data, 4);
        if (r->mode == MODE_VIF && path != 3) {
            r->packets_missing++; /* recorded, and not made again */
            return;
        }
        send(r, (uint32_t)index, (int)path, rec->data + 4, rec->size - 4);
        break;
    case KF4GS_VIFDATA:
        if (r->mode == MODE_VIF) {
            if (nd_oracle_on()) {
                nd280_oracle_vifdata((const uint32_t *)rec->data, rec->size / 4, current_program);
            }
            port_vif1_feed((const uint32_t *)rec->data, rec->size / 4);
        }
        break;
    case KF4GS_VU1_RUN:
        if (r->mode == MODE_GIF) {
            uint32_t address;

            memcpy(&address, rec->data, 4);
            port_gs_capture_vu1_run(address, 0, 0);
        }
        break;
    case KF4GS_READ_FIFO:
        read_fifo(r, rec);
        break;
    case KF4GS_VSYNC:
        vsync(r, rec);
        break;
    case KF4GS_ND_MESH:
    case KF4GS_ND_DRAWS:
        if (rd_replay_record(rec->type, rec->data, rec->size) < 0) {
            fprintf(stderr, "record %zu (native draw, type %u) is short: left out\n", index, rec->type);
        }
        break;
    }
}

static size_t start_state(Run *r) {
    const Capture *c = r->capture;
    size_t i;

    port_vu1_reset();
    port_vif1_reset();
    for (i = 0; i < c->count; i++) {
        const Record *rec = &c->records[i];

        switch (rec->type) {
        case KF4GS_VRAM:
        case KF4GS_GS_REGS:
        case KF4GS_GS_PRIV:
        case KF4GS_GS_HISTORY:
            if ((r->render & RENDER_GS) && !port_gs_backend_restore(rec->type, rec->data, rec->size)) {
                die("the capture's GS state (record %u) does not fit this GS", rec->type);
            }
            if ((r->render & RENDER_NATIVE) && !gsn_restore(rec->type, rec->data, rec->size)) {
                die("the capture's GS state (record %u) does not fit the native GS", rec->type);
            }
            break;
        case KF4GS_VU1: {
            Kf4GsVu1Regs regs;

            if (rec->size < 32768 + sizeof(regs)) {
                die("the capture's VU1 record is short");
            }
            memcpy(vu1_micro_memory, rec->data, 16384);
            memcpy(vu1_data_memory, rec->data + 16384, 16384);
            memcpy(&regs, rec->data + 32768, sizeof(regs));
            port_vu1_debug_regs(&regs, 1);
            port_vu1_micro_written();
            break;
        }
        case KF4GS_VIF1: {
            Kf4GsVif1Regs regs;

            memcpy(&regs, rec->data, sizeof(regs));
            port_vif1_debug_regs(&regs, 1);
            break;
        }
        case KF4GS_GIF:
        case KF4GS_VIFDATA:
        case KF4GS_VU1_RUN:
        case KF4GS_READ_FIFO:
        case KF4GS_VSYNC:
        case KF4GS_ND_MESH:
        case KF4GS_ND_DRAWS:
            return i;
        }
    }
    return i;
}

static void run_capture(Run *r) {
    size_t i = start_state(r);
    Kf4GsVu1Regs vu_start;

    port_vu1_debug_regs(&vu_start, 0); /* VU1's clock as the capture starts */
    rd_replay_reset();
    port_options.stats = r->mode == MODE_VIF; /* vu1.c counts its runs */
    memset(&port_stats, 0, sizeof(port_stats));
    port_stats.vu1_cycles = vu_start.cycle;
    gif_init(&r->gif, r->capture);
    current_program = 0xFFFFFFFEu;
    current_kick = 0;
    vif_run = r;
    port_gs_capture_recording = r->mode == MODE_VIF;
    for (r->cursor = i; r->cursor < r->capture->count;) {
        size_t index = r->cursor++;

        if (r->capture->records[index].type == KF4GS_END) {
            break;
        }
        handle(r, index);
    }
    port_gs_capture_recording = 0;
    if (rd_replay_stats.meshes || rd_replay_stats.draws || rd_replay_stats.draws_dropped) {
        printf("native draw: %llu draws and %llu meshes fed back (%llu meshes refused, %llu draws left out)",
               (unsigned long long)rd_replay_stats.draws, (unsigned long long)rd_replay_stats.meshes,
               (unsigned long long)rd_replay_stats.meshes_refused, (unsigned long long)rd_replay_stats.draws_dropped);
        if (r->render & RENDER_NATIVE) {
            printf("; the native GS drew %llu (%llu batches), lost %llu, skipped %llu PS2-path writes",
                   (unsigned long long)gsn_stats.nd_draws, (unsigned long long)gsn_stats.nd_batches,
                   (unsigned long long)gsn_stats.nd_lost, (unsigned long long)gsn_stats.nd_skipped);
        }
        printf("\n");
    }
    if (r->compare_renderers && r->seen_vsync) {
        /* a capture's last frame is drawn after its last vblank: compared once all of it is */
        compare_display(r, r->last_priv, "end", r->last_vblank);
        /* and the other frame buffer in play (0 and 0x8C, 640x448): the game keeps the last frame there
           for the next to blend with, which the displayed one shows only a frame later */
        {
            uint64_t other[0xF0 / 8];
            uint64_t fbp;

            memcpy(other, r->last_priv, sizeof(other));
            fbp = other[9 * 2] & 0x1FF;
            if (fbp == 0 || fbp == 0x8C) {
                other[9 * 2] = (other[9 * 2] & ~(uint64_t)0x1FF) | (fbp == 0 ? 0x8C : 0);
                compare_display(r, other, "end-other", r->last_vblank);
            }
        }
        printf("scanouts: %u pictures compared, %u differ by more than %u, at most %u\n", r->scanouts,
               r->scanouts_differ, r->threshold, r->scanout_worst);
    }
    if (r->mode == MODE_VIF) {
        printf("VIF1 -> VU1: %llu packets as recorded, %llu differ, %llu recorded but not made, %llu made but not "
               "recorded\n",
               (unsigned long long)r->packets_same, (unsigned long long)r->packets_differ,
               (unsigned long long)r->packets_missing, (unsigned long long)r->packets_extra);
        printf("VU1: %llu runs, %llu instructions, %llu cycles\n", (unsigned long long)port_stats.vu1_runs,
               (unsigned long long)port_stats.vu1_insns, (unsigned long long)(port_stats.vu1_cycles - vu_start.cycle));
        printf("VU1 time: %.3f ms (the GIF hand-offs excluded)\n", (double)port_stats.vu1_ticks / 1e6);
        if (port_options.vu1 == PORT_VU1_HLE) {
            printf("VU1 HLE: %llu runs, %.3f ms\n", (unsigned long long)port_stats.vu1_hle_runs,
                   (double)port_stats.vu1_hle_ticks / 1e6);
        } else if (port_options.vu1 == PORT_VU1_CHECK) {
            printf("VU1 check: %llu runs compared, %llu differ\n", (unsigned long long)port_vu1_check_runs,
                   (unsigned long long)port_vu1_check_differ);
        }
    }
    port_options.stats = 0;
}

/* ---- Listings ------------------------------------------------------------------------------------ */
static const char *prim_names[8] = {"point", "line", "lstrip", "tri", "tstrip", "tfan", "sprite", "?"};

static void list_draw(const Run *r, long i) {
    const Draw *d = &r->gif.draws[i];

    printf("%6ld rec %6u p%u ", i, d->record, d->path);
    if (d->path == 1) {
        printf("prog %03X kick %3u ", d->program & 0xFFF, d->kick);
    } else {
        printf("%18s", "");
    }
    printf("%-6s%s%s%s ctx%u v%5u k%5u box %4d,%4d-%4d,%4d z %08X-%08X", prim_names[d->prim & 7],
           (d->prim & 0x10) ? " tex" : "", (d->prim & 0x40) ? " abe" : "", (d->prim & 0x20) ? " fog" : "",
           (d->prim >> 9) & 1, d->vertices, d->kicks, d->box[0], d->box[1], d->box[2], d->box[3], d->zmin, d->zmax);
    printf(" | FRAME fbp %u fbw %u psm %02X fbmsk %08X | ZBUF zbp %u psm %02X zmsk %u",
           (unsigned)(d->frame & 0x1FF), (unsigned)((d->frame >> 16) & 0x3F), (unsigned)((d->frame >> 24) & 0x3F),
           (unsigned)(d->frame >> 32), (unsigned)(d->zbuf & 0x1FF), 0x30u | (unsigned)((d->zbuf >> 24) & 0xF),
           (unsigned)((d->zbuf >> 32) & 1));
    printf(" | TEST ate %u atst %u aref %02X afail %u date %u datm %u zte %u ztst %u",
           (unsigned)(d->test & 1), (unsigned)((d->test >> 1) & 7), (unsigned)((d->test >> 4) & 0xFF),
           (unsigned)((d->test >> 12) & 3), (unsigned)((d->test >> 14) & 1), (unsigned)((d->test >> 15) & 1),
           (unsigned)((d->test >> 16) & 1), (unsigned)((d->test >> 17) & 3));
    printf(" | ALPHA %u%u%u%u fix %02X fba %u pabe %u", (unsigned)(d->alpha & 3), (unsigned)((d->alpha >> 2) & 3),
           (unsigned)((d->alpha >> 4) & 3), (unsigned)((d->alpha >> 6) & 3), (unsigned)((d->alpha >> 32) & 0xFF),
           (unsigned)(d->fba & 1), (unsigned)(d->pabe & 1));
    printf(" | TEX0 %016llX TEX1 %016llX CLAMP %011llX", (unsigned long long)d->tex0, (unsigned long long)d->tex1,
           (unsigned long long)d->clamp);
    printf(" | SCISSOR %u,%u-%u,%u XYOFFSET %.4g,%.4g%s\n", (unsigned)(d->scissor & 0x7FF),
           (unsigned)((d->scissor >> 32) & 0x7FF), (unsigned)((d->scissor >> 16) & 0x7FF),
           (unsigned)((d->scissor >> 48) & 0x7FF), (d->xyoffset & 0xFFFF) / 16.0,
           ((d->xyoffset >> 32) & 0xFFFF) / 16.0, d->differs ? " | DIFFERS from the recorded packet" : "");
}

static void list_vertices(const Run *r, long i) {
    const Draw *d;
    uint32_t n;

    if (i < 0 || (size_t)i >= r->gif.count) {
        die("there is no draw %ld (%zu draws)", i, r->gif.count);
    }
    d = &r->gif.draws[i];
    list_draw(r, i);
    for (n = 0; n < d->vertices; n++) {
        char what[16];

        snprintf(what, sizeof(what), "%4u", n);
        print_vertex(what, &r->gif.vertices[d->first_vertex + n]);
    }
}

/* draw by draw: the GS state and the vertices */
static void compare(const Run *a, const Run *b, int limit) {
    size_t i, n = a->gif.count < b->gif.count ? a->gif.count : b->gif.count, differing = 0;

    printf("draws: %zu and %zu\n", a->gif.count, b->gif.count);
    for (i = 0; i < n; i++) {
        const Draw *x = &a->gif.draws[i], *y = &b->gif.draws[i];
        const uint64_t *sx = &x->tex0, *sy = &y->tex0;
        static const char *names[] = {"TEX0", "TEX1", "CLAMP", "TEST", "ZBUF", "ALPHA", "FBA", "FRAME", "SCISSOR",
                                      "XYOFFSET", "PABE", "COLCLAMP", "TEXA", "FOGCOL", "DTHE", "DIMX", "TEXCLUT"};
        int state = 0, k;
        uint32_t v, vdiff = 0, first = 0;
        int64_t dz_max = 0;

        for (k = 0; k < 17; k++) {
            state |= sx[k] != sy[k];
        }
        state |= x->prim != y->prim || x->vertices != y->vertices || x->path != y->path;
        for (v = 0; v < x->vertices && v < y->vertices; v++) {
            const Vertex *p = &a->gif.vertices[x->first_vertex + v], *q = &b->gif.vertices[y->first_vertex + v];
            int64_t dz = (int64_t)p->z - (int64_t)q->z;

            if (memcmp(p, q, sizeof(Vertex)) != 0) {
                if (vdiff++ == 0) {
                    first = v;
                }
                if (dz < 0) dz = -dz;
                if (dz > dz_max) dz_max = dz;
            }
        }
        if (!state && !vdiff) {
            continue;
        }
        if (differing++ >= (size_t)limit) {
            continue;
        }
        printf("draw %zu differs:", i);
        if (x->prim != y->prim) printf(" PRIM %03X/%03X", x->prim, y->prim);
        if (x->vertices != y->vertices) printf(" vertices %u/%u", x->vertices, y->vertices);
        for (k = 0; k < 17; k++) {
            if (sx[k] != sy[k]) {
                printf(" %s %llX/%llX", names[k], (unsigned long long)sx[k], (unsigned long long)sy[k]);
            }
        }
        if (vdiff) {
            printf(" %u vertices differ (largest Z difference %lld), the first:\n", vdiff, (long long)dz_max);
            print_vertex("a", &a->gif.vertices[x->first_vertex + first]);
            print_vertex("b", &b->gif.vertices[y->first_vertex + first]);
        } else {
            printf("\n");
        }
    }
    printf("%zu of %zu draws differ\n", differing + (a->gif.count > n ? a->gif.count - n : b->gif.count - n),
           a->gif.count > b->gif.count ? a->gif.count : b->gif.count);
}

static void info(const Capture *c) {
    size_t i, counts[32] = {0};
    uint64_t bytes[32] = {0};
    static const char *names[32] = {[0] = "END", [1] = "META", [2] = "VRAM", [3] = "GS_REGS", [4] = "GS_PRIV",
                                    [5] = "GS_HISTORY", [6] = "VU1", [7] = "VIF1", [16] = "GIF", [17] = "VIFDATA",
                                    [18] = "VU1_RUN", [19] = "READ_FIFO", [20] = "VSYNC",
                                    [21] = "ND_MESH", [22] = "ND_DRAWS"};

    printf("%s", c->meta);
    for (i = 0; i < c->count; i++) {
        uint32_t t = c->records[i].type < 32 ? c->records[i].type : 31;

        counts[t]++;
        bytes[t] += c->records[i].size;
    }
    printf("records: %zu, %.1f MB\n", c->count, (double)c->body_size / 1048576.0);
    for (i = 0; i < 32; i++) {
        if (counts[i]) {
            printf("  %-10s %8zu  %10llu bytes\n", names[i] ? names[i] : "?", counts[i], (unsigned long long)bytes[i]);
        }
    }
}

static void usage(void) {
    fprintf(stderr,
            "usage: kf4-gsreplay FILE.kf4gs [--info] [--mode gif|vif] [--frames] [--out DIR] [--list]\n"
            "                    [--draw N | --draws A-B] [--zbuf] [--pixel X,Y [--fbp N]] [--vertices N]\n"
            "                    [--compare OTHER.kf4gs] [--compare-mode gif|vif] [--vu-round nearest]\n"
            "                    [--vu1 interp|hle|check] [--diff N] [--limit N]\n"
            "                    [--renderer gs|native|both] [--compare-renderers] [--msaa N]\n"
            "                    [--scale X[,Y]] [--lod scale|ps2|full] [--cutout-aa off|ssaa] [--layer-z ps2|tolerant]\n"
            "                    [--nd-oracle]\n"
            "                    [--native-draw native|ps2]\n"
            "(tools/README.md; the top of tools/gsreplay/gsreplay.c says what each does)\n");
    exit(2);
}

int main(int argc, char **argv) {
    Capture capture, other;
    Run run;
    const char *file = NULL, *compare_file = NULL;
    int list = 0, show_info = 0, compare_mode = -1, limit = 50, i;
    long vertices = -1;
    int renderer = RENDER_GS;
    int exact = 0, msaa = 1, scale_x = 1, scale_y = 1, lod = 1, cutout_aa = GSN_CUTOUT_AA_OFF, colour = GSN_COLOUR_EXACT;
    int layer_z = GSN_LAYER_Z_PS2;
    int nd_oracle = 0, vu1_given = 0;
    int native_draw = GSN_NATIVE_DRAW_NATIVE;

    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    memset(&run, 0, sizeof(run));
    run.draw_from = run.draw_to = -1;
    run.fbp_filter = -1;
    run.diff_vertices = 8;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;

        if (strcmp(a, "--info") == 0) {
            show_info = 1;
        } else if (strcmp(a, "--list") == 0) {
            list = 1;
        } else if (strcmp(a, "--frames") == 0) {
            run.frames = 1;
        } else if (strcmp(a, "--zbuf") == 0) {
            run.zbuf = 1;
        } else if (strcmp(a, "--exact") == 0) {
            exact = 1;
        } else if (strcmp(a, "--no-resync") == 0) {
            run.resync = -1;
        } else if (strcmp(a, "--nd-oracle") == 0) {
            nd_oracle = 1;
        } else if (strcmp(a, "--compare-renderers") == 0) {
            run.compare_renderers = 1;
            renderer = RENDER_GS | RENDER_NATIVE;
        } else if (strcmp(a, "--renderer") == 0 && v != NULL) {
            renderer = strcmp(v, "gs") == 0 ? RENDER_GS : strcmp(v, "native") == 0 ? RENDER_NATIVE
                       : strcmp(v, "both") == 0 ? RENDER_GS | RENDER_NATIVE : 0;
            if (renderer == 0) {
                usage();
            }
            i++;
        } else if (a[0] == '-' && v == NULL) {
            usage();
        } else if (strcmp(a, "--msaa") == 0) {
            msaa = atoi(v);
            i++;
        } else if (strcmp(a, "--lod") == 0) {
            lod = strcmp(v, "ps2") == 0 ? 0 : strcmp(v, "full") == 0 ? 2 : 1;
            i++;
        } else if (strcmp(a, "--cutout-aa") == 0) {
            cutout_aa = strcmp(v, "ssaa") == 0 ? GSN_CUTOUT_AA_SSAA : GSN_CUTOUT_AA_OFF;
            i++;
        } else if (strcmp(a, "--layer-z") == 0) {
            layer_z = strcmp(v, "tolerant") == 0 ? GSN_LAYER_Z_TOLERANT : GSN_LAYER_Z_PS2;
            i++;
        } else if (strcmp(a, "--native-draw") == 0) {
            native_draw = strcmp(v, "ps2") == 0      ? GSN_NATIVE_DRAW_PS2
                          : strcmp(v, "native") == 0 ? GSN_NATIVE_DRAW_NATIVE
                                                     : -1;
            if (native_draw < 0) {
                usage();
            }
            i++;
        } else if (strcmp(a, "--colour") == 0) {
            colour = strcmp(v, "high") == 0 ? GSN_COLOUR_HIGH : GSN_COLOUR_EXACT;
            i++;
        } else if (strcmp(a, "--scale") == 0) {
            const char *comma = strchr(v, ',');
            scale_x = atoi(v);
            scale_y = comma ? atoi(comma + 1) : scale_x;
            i++;
        } else if (strcmp(a, "--mode") == 0 || strcmp(a, "--compare-mode") == 0) {
            int m = strcmp(v, "vif") == 0 ? MODE_VIF : strcmp(v, "gif") == 0 ? MODE_GIF : -1;

            if (m < 0) {
                usage();
            }
            if (a[2] == 'm') {
                run.mode = m;
            } else {
                compare_mode = m;
            }
            i++;
        } else if (strcmp(a, "--out") == 0) {
            run.out = v;
            i++;
        } else if (strcmp(a, "--draw") == 0) {
            run.draw_from = run.draw_to = strtol(v, NULL, 0);
            i++;
        } else if (strcmp(a, "--draws") == 0) {
            if (sscanf(v, "%ld-%ld", &run.draw_from, &run.draw_to) != 2) {
                usage();
            }
            i++;
        } else if (strcmp(a, "--pixel") == 0) {
            if (sscanf(v, "%d,%d", &run.pixel_x, &run.pixel_y) != 2) {
                usage();
            }
            run.pixel = 1;
            i++;
        } else if (strcmp(a, "--fbp") == 0) {
            run.fbp_filter = strtol(v, NULL, 0);
            i++;
        } else if (strcmp(a, "--vertices") == 0) {
            vertices = strtol(v, NULL, 0);
            i++;
        } else if (strcmp(a, "--compare") == 0) {
            compare_file = v;
            i++;
        } else if (strcmp(a, "--vu-round") == 0) {
            if (strcmp(v, "nearest") == 0) {
                port_vu1_round_nearest = 1;
            } else if (strcmp(v, "zero") != 0) {
                usage();
            }
            i++;
        } else if (strcmp(a, "--vu1") == 0) {
            vu1_given = 1;
            if (strcmp(v, "interp") == 0) {
                port_options.vu1 = PORT_VU1_INTERP;
            } else if (strcmp(v, "hle") == 0) {
                port_options.vu1 = PORT_VU1_HLE;
            } else if (strcmp(v, "check") == 0) {
                port_options.vu1 = PORT_VU1_CHECK;
            } else {
                usage();
            }
            i++;
        } else if (strcmp(a, "--diff") == 0) {
            run.diff_vertices = (int)strtol(v, NULL, 0);
            i++;
        } else if (strcmp(a, "--limit") == 0) {
            limit = (int)strtol(v, NULL, 0);
            i++;
        } else if (a[0] == '-' || file != NULL) {
            usage();
        } else {
            file = a;
        }
    }
    if (file == NULL) {
        usage();
    }
    load_capture(file, &capture);
    if (show_info) {
        info(&capture);
    }
    run.capture = &capture;
    port_options.deinterlace = (int)meta_int(&capture, "deinterlace", 1);
    port_options.progressive = (int)meta_int(&capture, "progressive", 1);
    port_options.anti_blur = (int)meta_int(&capture, "anti_blur", 1);
    port_options.adapt_width = (int)meta_int(&capture, "adapt_width", 1);
    port_options.whole_frame = (int)meta_int(&capture, "whole_frame", 0);
    port_options.dump_dir = run.out;
    run.limit = limit;
    run.threshold = exact ? 0 : 1;
    run.resync = run.resync == 0 && run.compare_renderers;
    run.render = run.frames || run.draw_from >= 0 || run.pixel || run.compare_renderers ? renderer : 0;
    if (run.render) {
        if ((run.render & RENDER_GS) && !port_gs_backend_open()) {
            die("no paraLLEl-GS: pictures need a Vulkan GPU");
        }
        if ((run.render & RENDER_NATIVE) && !gsn_open(NULL)) {
            die("no native GS: it needs a Vulkan GPU");
        }
        if ((run.render & RENDER_NATIVE) && msaa > 1) {
            fprintf(stderr, "native GS: MSAA %dx\n", gsn_set_msaa(msaa));
        }
        if ((run.render & RENDER_NATIVE) && (scale_x > 1 || scale_y > 1)) {
            gsn_set_scale(scale_x, scale_y);
            run.scaled = 1;
        }
        if (run.render & RENDER_NATIVE) {
            gsn_set_lod(lod);
            gsn_set_cutout_aa(cutout_aa);
            gsn_set_colour(colour);
            gsn_set_layer_z(layer_z);
            gsn_set_native_draw(native_draw);
        }
        run.frame.rgba = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
        run.native_frame.rgba = malloc(sizeof(uint32_t) * PORT_GS_FRAME_PIXELS);
    }
    if (nd_oracle) {
        run.mode = MODE_VIF;
        if (!vu1_given) {
            port_options.vu1 = PORT_VU1_HLE;
        }
        if (!(run.render & RENDER_NATIVE) && !gsn_open(NULL)) {
            die("--nd-oracle: no native GS: it needs a Vulkan GPU");
        }
        nd_oracle_begin(vu1_data_memory, limit);
    }
    if (show_info && !list && !run.render && vertices < 0 && compare_file == NULL && run.mode == MODE_GIF) {
        return 0;
    }
    run_capture(&run);
    nd_oracle_end();
    printf("%zu draws, %zu vertices\n", run.gif.count, run.gif.vertex_count);
    if (run.compare_renderers) {
        printf("renderers: %llu draws compared, %llu differ\n", (unsigned long long)run.compared,
               (unsigned long long)run.compared_differ);
    }
    if (list) {
        long d;

        for (d = 0; d < (long)run.gif.count; d++) {
            list_draw(&run, d);
        }
    }
    if (vertices >= 0) {
        list_vertices(&run, vertices);
    }
    if (compare_file != NULL || compare_mode >= 0) {
        Run b;

        memset(&b, 0, sizeof(b));
        b.draw_from = b.draw_to = -1;
        b.fbp_filter = -1;
        if (compare_file != NULL) {
            load_capture(compare_file, &other);
            b.capture = &other;
        } else {
            b.capture = &capture;
        }
        b.mode = compare_mode >= 0 ? compare_mode : run.mode;
        run_capture(&b);
        compare(&run, &b, limit);
    }
    fflush(stdout);
    return 0;
}
