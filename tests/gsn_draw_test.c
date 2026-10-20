/* The native GS's drawing (port/gs/native/gsn.cpp and its shaders) against paraLLEl-GS as the oracle.

   Each case is a random but valid GS drawing scenario, made up as GIF packets and sent to both
   renderers alike: random pixels uploaded into a frame buffer and a Z buffer, a random texture and
   CLUT uploaded beside them (IMAGE transfers), the drawing registers, then a batch of primitives.
   Afterwards the frame buffer (all four bytes a pixel) and the Z buffer are read from each one's VRAM
   and compared. What a case draws covers what the game asks of the GS (docs/port/STEP3.md, the
   survey's table) and a little around it:
   - sprites, triangle lists and strips (and the odd fan), overlapping; flat and Gouraud; context 1
     or 2; the registers by A+D or by PACKED descriptors (RGBAQ, ST, UV, XYZF2, XYZ2), on PATH2 or
     PATH3; XYOFFSET with a fraction, vertices with fractions;
   - textures: PSMT8 and PSMT4 through a CT32 (sometimes CT16) CLUT, CSM1, CSA, the CLD modes (0 keeps
     the CLUT buffer of the last case, 4 and 5 compare CBP0/1); PSMCT32, CT24, CT16 and CT16S direct;
     UV or STQ (Q 0.5 to 2); nearest or bilinear; REPEAT or CLAMP; MODULATE, DECAL, HIGHLIGHT,
     HIGHLIGHT2, TCC; TEXA; sprites that read their own frame buffer (TBP = FBP), shifted;
   - fog (XYZF2's F, FOGCOL); the alpha test (every ATST, any AREF, every AFAIL); Z: GEQUAL, GREATER,
     ALWAYS (rarely NEVER) into Z32, Z24, Z16 and Z16S buffers, ZMSK; blending: any A, B, C, D (C = FIX
     with any FIX, As above 0x80), COLCLAMP, FBA, PABE, DATE with DATM, FBMSK (whole bytes and random
     bits); SCANMSK; frame buffers CT32, CT24, CT16, CT16S.

   The pass criterion. paraLLEl-GS emulates the GS's own scan conversion and fixed-point interpolation;
   the native GS uses Vulkan's rasteriser and float interpolation, and accepts (docs/port/PLAN.md,
   "Where the native GS differs from paraLLEl-GS"):
   - pixels on a triangle's edge, covered by one and not the other: a pixel whose integer point (the
     GS's sample) is within 1 of a triangle's edge is not compared ("edge" below). Sprites' edges are
     exact and compared.
   - (Cs - Cd) C + Cd on non-sprite primitives: Vulkan rounds to nearest where the GS truncates, 1 a
     layer; colours and texture coordinates interpolated in floats: 1 off in a colour, a texel boundary
     crossed a pixel early or late (with nearest filtering, any difference: the textures are noise);
   - the alpha test cutting an interpolated or filtered alpha at AREF a little differently (and what
     overlapping primitives then blend with);
   - ST/Q: at a texel's edge the two land either side now and then; with bilinear filtering every
     sixteenth of a texel is such an edge, and noise turns it into differences of up to 30;
   - Z interpolated in floats: 2 apart plus 1 in 2^20; a Z32 buffer above 2^24 (a D32 float holds 24
     bits): 1 in 2^23 for sprites, 1 in 2^16 interpolated.
   So a pixel "differs" when a channel is more than 1 apart (a CT16 buffer's: 1 in 5 bits; with
   COLCLAMP 0, apart either way round 256). A case passes when, of the pixels not on a triangle's edge:
   - sprites with no texture, sprites that map texels 1:1 (UV a texel a pixel), and nearest-filtered
     sprites that read their own frame buffer: none differ, in colour or Z;
   - STQ with bilinear filtering: at most 1/8 of the pixels the case covers differ, plus 4;
   - anything else: at most 1/50 of them, plus 4.
   The limits are loose on purpose: a systematic error (a wrong blend, format, wrap, CLUT) moves most
   pixels of a primitive, and the strict class catches a difference of a single pixel.

   Left out, as paraLLEl-GS quirks the game does not meet (PLAN.md has them): PABE with (A - A) C + Cd;
   sprites drawn right to left or bottom to top (GSN_REVERSE=1 draws some); a draw reading its own
   frame buffer past the buffer, or cut by the scissor (feedback sprites read inside the buffer and are
   not scissored).

   gsn_draw_test [cases [seed]]: cases (300 by default) from the seed (1). Each failure prints the
   case's parameters, its pixels differing by more than 1 and the largest difference. The cases share
   one GS (targets, the CLUT buffer), as a game's draws do, so a case can depend on the ones before.
   Switches, for narrowing a difference down: GSN_OFF=feature,... (make_case's list) turns features
   off; GSN_CASE=n runs one case, GSN_FIRST=n the cases from n; GSN_THRESHOLD=t (1); GSN_ALL prints
   every case; GSN_VERBOSE the vertices; GSN_DUMP writes a failing case's two frame buffers and their
   difference as PPM files; GSN_GRADIENT makes CT32 textures their own coordinates (decal), and prints
   how far apart the texels each renderer sampled are. Then as many rounds of buffers that share
   memory (test_shared_memory, below; GSN_ROUND, GSN_SKIP, GSN_NO_SHARED). Exits 77 (skipped) when
   there is no Vulkan device. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gsn.h"
#include "gsn_vram.h"
#include "port/platform.h"

int port_gs_backend_open(void);
void port_gs_backend_gif(int path, const void *data, size_t bytes);
const void *port_gs_backend_vram(void);

/* What gs_parallel.cpp needs of the program */
PortOptions port_options;

/* ---- Random numbers --------------------------------------------------------------------------- */
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* 0..n-1 */
static uint32_t rnd(uint32_t n) {
    return n ? (uint32_t)(rng() % n) : 0;
}

/* 1 in n */
static int one_in(uint32_t n) {
    return rnd(n) == 0;
}

static float rndf(float lo, float hi) {
    return lo + (hi - lo) * (float)(rng() >> 40) / (float)(1u << 24);
}

static void fill_random(uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)(rng() >> 24);
    }
}

/* ---- GIF packets ------------------------------------------------------------------------------ */
static uint64_t *packet;
static size_t packet_qw, packet_cap;

static void put_qword(uint64_t lo, uint64_t hi) {
    if (packet_qw == packet_cap) {
        packet_cap = packet_cap ? packet_cap * 2 : 4096;
        packet = realloc(packet, packet_cap * 16);
    }
    packet[packet_qw * 2] = lo;
    packet[packet_qw * 2 + 1] = hi;
    packet_qw++;
}

static uint64_t gif_tag(uint32_t nloop, int eop, uint32_t flg, uint32_t nreg) {
    return (uint64_t)nloop | (uint64_t)(eop ? 1 : 0) << 15 | (uint64_t)flg << 58 | (uint64_t)(nreg & 15) << 60;
}

/* The packet so far, to both renderers (or to neither: test_shared_memory's GSN_SKIP) */
static int discard;

static void send_both(int path) {
    if (packet_qw && !discard) {
        port_gs_backend_gif(path, packet, packet_qw * 16);
        gsn_gif(path, packet, packet_qw * 16);
    }
    packet_qw = 0;
}

/* Register writes, gathered and sent as one A+D tag */
static uint64_t ad_list[256][2];
static int ad_count;

static void ad(uint32_t reg, uint64_t value) {
    ad_list[ad_count][0] = value;
    ad_list[ad_count][1] = reg;
    ad_count++;
}

static void put_ad(int eop) {
    if (!ad_count) {
        return;
    }
    put_qword(gif_tag((uint32_t)ad_count, eop, 0, 1), 0xE);
    for (int i = 0; i < ad_count; i++) {
        put_qword(ad_list[i][0], ad_list[i][1]);
    }
    ad_count = 0;
}

enum {
    R_PRIM = 0x00, R_RGBAQ = 0x01, R_ST = 0x02, R_UV = 0x03, R_XYZF2 = 0x04, R_XYZ2 = 0x05, R_TEX0_1 = 0x06,
    R_CLAMP_1 = 0x08, R_FOG = 0x0A, R_TEX1_1 = 0x14, R_XYOFFSET_1 = 0x18, R_PRMODECONT = 0x1A, R_TEXCLUT = 0x1C,
    R_SCANMSK = 0x22, R_TEXA = 0x3B, R_FOGCOL = 0x3D, R_TEXFLUSH = 0x3F, R_SCISSOR_1 = 0x40, R_ALPHA_1 = 0x42,
    R_DTHE = 0x45, R_COLCLAMP = 0x46, R_TEST_1 = 0x47, R_PABE = 0x49, R_FBA_1 = 0x4A, R_FRAME_1 = 0x4C,
    R_ZBUF_1 = 0x4E, R_BITBLTBUF = 0x50, R_TRXPOS = 0x51, R_TRXREG = 0x52, R_TRXDIR = 0x53,
};

/* A host-to-local transfer of these bytes into a buffer, to both, on PATH2 */
static void upload(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   const uint8_t *data) {
    size_t bytes = ((size_t)w * h * (size_t)gsn_psm_bits(psm) + 7) / 8;
    size_t qwords = (bytes + 15) / 16;

    ad(R_BITBLTBUF, (uint64_t)bp << 32 | (uint64_t)bw << 48 | (uint64_t)psm << 56);
    ad(R_TRXPOS, (uint64_t)x << 32 | (uint64_t)y << 48);
    ad(R_TRXREG, (uint64_t)w | (uint64_t)h << 32);
    ad(R_TRXDIR, 0);
    put_ad(0);
    for (size_t done = 0; done < qwords;) {
        size_t n = qwords - done > 32767 ? 32767 : qwords - done;

        put_qword(gif_tag((uint32_t)n, done + n == qwords, 2, 0), 0);
        for (size_t i = 0; i < n; i++) {
            uint64_t q[2] = {0, 0};
            size_t at = (done + i) * 16;

            memcpy(q, data + at, bytes - at < 16 ? bytes - at : 16);
            put_qword(q[0], q[1]);
        }
        done += n;
    }
    send_both(2);
}

/* paraLLEl-GS's view of VRAM (port_gs_backend_vram) is a mirror refreshed only for pages a transfer
   touched: a rectangle copied onto itself has it refresh the pages draws wrote */
static void parallel_refresh(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h) {
    uint64_t q[2 * 5] = {
        gif_tag(4, 1, 0, 1), 0xE,
        (uint64_t)bp | (uint64_t)bw << 16 | (uint64_t)psm << 24 | (uint64_t)bp << 32 | (uint64_t)bw << 48 | (uint64_t)psm << 56,
        R_BITBLTBUF, 0, R_TRXPOS, (uint64_t)w | (uint64_t)h << 32, R_TRXREG, 2, R_TRXDIR,
    };
    port_gs_backend_gif(2, q, sizeof(q));
}

/* ---- A case ----------------------------------------------------------------------------------- */
typedef struct {
    float x, y;      /* pixels, in the frame buffer */
    uint32_t z;
    uint32_t rgba;
    float s, t, q;   /* STQ */
    uint32_t u, v;   /* UV, 10.4 */
    uint32_t fog;
} Vert;

#define MAX_PRIMS 16
#define MAX_VERTS (MAX_PRIMS * 3 + 2)

typedef struct {
    uint32_t seed;
    /* the buffers */
    uint32_t fbp, fbw, fpsm, width, height, fbmsk; /* fbp in pages */
    uint32_t zbp, zpsm, zmsk;
    /* PRIM */
    uint32_t prim, iip, tme, fge, abe, fst, ctxt;
    uint32_t nprims, nverts;
    Vert v[MAX_VERTS];
    /* texture */
    uint32_t tbp, tbw, tpsm, tw, th, tcc, tfx, cbp, cpsm, csa, cld, feedback;
    uint32_t mmag, mmin, wms, wmt, minu, maxu, minv, maxv;
    uint32_t ta0, aem, ta1;
    int unit_uv; /* sprites whose UV step a texel a pixel */
    /* TEST */
    uint32_t ate, atst, aref, afail, date, datm, ztst;
    /* ALPHA and the rest */
    uint32_t a, b, c, d, fix, colclamp, fba, pabe, scanmsk;
    uint32_t sx0, sx1, sy0, sy1;
    uint32_t ofx, ofy; /* XYOFFSET, 12.4 */
    uint32_t fogcol;
    int packed, path;
} Case;

static const char *psm_name(uint32_t psm) {
    switch (psm) {
    case GSN_PSMCT32: return "CT32";
    case GSN_PSMCT24: return "CT24";
    case GSN_PSMCT16: return "CT16";
    case GSN_PSMCT16S: return "CT16S";
    case GSN_PSMT8: return "T8";
    case GSN_PSMT4: return "T4";
    case GSN_PSMZ32: return "Z32";
    case GSN_PSMZ24: return "Z24";
    case GSN_PSMZ16: return "Z16";
    case GSN_PSMZ16S: return "Z16S";
    }
    return "?";
}

static void print_case(const Case *k) {
    static const char *prims[] = {"point", "line", "linestrip", "tri", "strip", "fan", "sprite", "?"};
    static const char *ztsts[] = {"NEVER", "ALWAYS", "GEQUAL", "GREATER"};
    static const char *tfxs[] = {"MODULATE", "DECAL", "HIGHLIGHT", "HIGHLIGHT2"};

    printf("  %s x%u IIP %u FGE %u ABE %u CTXT %u %s path %d; FRAME %s FBP %#x FBW %u %ux%u FBMSK %08X; "
           "ZBUF %s ZBP %#x ZMSK %u ZTST %s\n",
           prims[k->prim], k->nprims, k->iip, k->fge, k->abe, k->ctxt, k->packed ? "PACKED" : "A+D", k->path,
           psm_name(k->fpsm), k->fbp, k->fbw, k->width, k->height, k->fbmsk, psm_name(k->zpsm), k->zbp, k->zmsk,
           ztsts[k->ztst]);
    if (k->tme) {
        printf("  TEX %s%s TBP %#x TBW %u %ux%u TCC %u %s FST %u%s; CLUT CBP %#x %s CSA %u CLD %u; MMAG %u MMIN %u "
               "WMS %u WMT %u (%u-%u, %u-%u); TEXA %02X %u %02X\n",
               psm_name(k->tpsm), k->feedback ? " (its own frame buffer)" : "", k->tbp, k->tbw, 1u << k->tw,
               1u << k->th, k->tcc, tfxs[k->tfx], k->fst, k->unit_uv ? " (1:1)" : "", k->cbp,
               k->cpsm ? "CT16" : "CT32", k->csa, k->cld, k->mmag, k->mmin, k->wms, k->wmt, k->minu, k->maxu,
               k->minv, k->maxv, k->ta0, k->aem, k->ta1);
    }
    printf("  TEST ATE %u ATST %u AREF %02X AFAIL %u DATE %u DATM %u; ALPHA A %u B %u C %u D %u FIX %02X; "
           "COLCLAMP %u FBA %u PABE %u SCANMSK %u; SCISSOR %u-%u %u-%u; XYOFFSET %u.%u %u.%u; FOGCOL %06X\n",
           k->ate, k->atst, k->aref, k->afail, k->date, k->datm, k->a, k->b, k->c, k->d, k->fix, k->colclamp, k->fba,
           k->pabe, k->scanmsk, k->sx0, k->sx1, k->sy0, k->sy1, k->ofx >> 4, k->ofx & 15, k->ofy >> 4, k->ofy & 15,
           k->fogcol);
    if (getenv("GSN_VERBOSE")) {
        for (uint32_t i = 0; i < k->nverts; i++) {
            const Vert *v = &k->v[i];
            printf("    v%u (%.4f, %.4f) z %08X rgba %08X stq %g %g %g uv %u.%u %u.%u fog %u\n", i, v->x, v->y, v->z,
                   v->rgba, v->s, v->t, v->q, v->u >> 4, v->u & 15, v->v >> 4, v->v & 15, v->fog);
        }
    }
}

static uint32_t ilog2(uint32_t v) {
    uint32_t l = 0;
    while ((1u << l) < v) {
        l++;
    }
    return l;
}

/* A vertex's position: whole sixteenths of a pixel */
static float sixteenths(float v) {
    return floorf(v * 16.0f) / 16.0f;
}

static uint32_t random_colour(void) {
    uint32_t c = (uint32_t)rng();
    /* alpha: mostly at or under 0x80, sometimes above */
    uint32_t a = one_in(4) ? rnd(256) : rnd(0x81);
    if (one_in(8)) {
        a = 0x80;
    }
    return (c & 0xFFFFFF) | a << 24;
}

static int off(const char *what);

static uint32_t random_z(const Case *k) {
    if (off("highz")) {
        return rnd(k->zpsm == GSN_PSMZ16 || k->zpsm == GSN_PSMZ16S ? 0x10000 : 1u << 24);
    }
    switch (k->zpsm) {
    case GSN_PSMZ32:
        return one_in(4) ? (uint32_t)rng() : rnd(1u << 24);
    case GSN_PSMZ24:
        return one_in(8) ? rnd(1u << 25) : rnd(1u << 24);
    default:
        return one_in(8) ? rnd(0x20000) : rnd(0x10000);
    }
}

/* GSN_OFF: features turned off, to narrow a difference down (a comma-separated list of: tex, blend,
   atest, date, fog, ztest, zmsk, fbmsk, scanmsk, colclamp, fba, pabe, scissor, frac, tri, sprite,
   fpsm, zpsm, gouraud, stq, bilinear, feedback, highz, multi) */
static const char *off_list;

static int off(const char *what) {
    const char *p = off_list;
    size_t n = strlen(what);
    while (p && *p) {
        if (strncmp(p, what, n) == 0 && (p[n] == ',' || p[n] == 0)) {
            return 1;
        }
        p = strchr(p, ',');
        p = p ? p + 1 : NULL;
    }
    return 0;
}

static void apply_off(Case *k) {
    if (off("tex")) k->tme = 0;
    if (off("blend")) k->abe = 0;
    if (off("atest")) k->ate = 0;
    if (off("date")) k->date = 0;
    if (off("fog")) k->fge = 0;
    if (off("ztest")) k->ztst = 1;
    if (off("zmsk")) k->zmsk = 1;
    if (off("fbmsk")) k->fbmsk = 0;
    if (off("scanmsk")) k->scanmsk = 0;
    if (off("colclamp")) k->colclamp = 1;
    if (off("fba")) k->fba = 0;
    if (off("pabe")) k->pabe = 0;
    if (off("gouraud")) k->iip = 0;
    if (off("stq")) k->fst = 1;
    if (off("bilinear")) k->mmag = k->mmin = 0;
    if (getenv("GSN_GRADIENT") && k->tme && !k->feedback) {
        k->tpsm = GSN_PSMCT32;
        k->tfx = 1;
    }
    if (off("scissor")) {
        k->sx0 = k->sy0 = 0;
        k->sx1 = k->width - 1;
        k->sy1 = k->height - 1;
    }
}

static void make_case(Case *k) {
    memset(k, 0, sizeof(*k));

    /* the frame buffer: pages 0-127; Z: 128-255; the texture 256-383; the CLUT 400-511 */
    static const uint32_t fpsms[] = {GSN_PSMCT32, GSN_PSMCT32, GSN_PSMCT32, GSN_PSMCT24, GSN_PSMCT16, GSN_PSMCT16S};
    static const uint32_t zpsms[] = {GSN_PSMZ32, GSN_PSMZ24, GSN_PSMZ16, GSN_PSMZ16S, GSN_PSMZ16S};
    k->fpsm = off("fpsm") ? GSN_PSMCT32 : fpsms[rnd(6)];
    k->fbw = 1 + rnd(4);
    k->width = k->fbw * 64;
    k->height = 32 + rnd(97);
    k->fbp = rnd(4) * 32 + rnd(8);
    k->zpsm = off("zpsm") ? GSN_PSMZ16S : zpsms[rnd(5)];
    k->zbp = 128 + rnd(4) * 32 + rnd(8);
    k->zmsk = one_in(4);
    switch (rnd(8)) {
    case 0:
        k->fbmsk = (uint32_t)rng();
        break;
    case 1:
        k->fbmsk = 0xFFu << (rnd(4) * 8);
        break;
    case 2:
        k->fbmsk = 0xFF000000u;
        break;
    default:
        k->fbmsk = 0;
        break;
    }

    /* the primitives */
    static const uint32_t prims[] = {6, 6, 6, 3, 3, 4, 4, 5};
    k->prim = prims[rnd(8)];
    if (off("tri")) {
        k->prim = 6;
    }
    if (off("sprite") && k->prim == 6) {
        k->prim = 4;
    }
    k->iip = rnd(2);
    k->tme = !one_in(3);
    k->fge = one_in(4);
    k->abe = rnd(2);
    k->ctxt = rnd(2);
    k->packed = rnd(2);
    k->path = one_in(3) ? 3 : 2;

    /* the texture */
    if (k->tme) {
        static const uint32_t tpsms[] = {GSN_PSMT8, GSN_PSMT8, GSN_PSMT4, GSN_PSMT4, GSN_PSMCT32,
                                         GSN_PSMCT24, GSN_PSMCT16, GSN_PSMCT16S};
        k->feedback = k->prim == 6 && (k->fpsm == GSN_PSMCT32 || k->fpsm == GSN_PSMCT16S) && one_in(5) && !off("feedback");
        if (k->feedback) {
            k->tpsm = k->fpsm;
            k->tbp = k->fbp * 32;
            k->tbw = k->fbw;
            k->tw = ilog2(k->width);
            k->th = ilog2(k->height);
        } else {
            k->tpsm = tpsms[rnd(8)];
            k->tw = 2 + rnd(7);
            k->th = 2 + rnd(7);
            uint32_t min_bw = (1u << k->tw) / 64;
            if (k->tpsm == GSN_PSMT8 || k->tpsm == GSN_PSMT4) {
                k->tbw = 2 * ((min_bw + 1) / 2 + rnd(2));
                if (k->tbw == 0) {
                    k->tbw = 2;
                }
            } else {
                k->tbw = (min_bw ? min_bw : 1) + rnd(2);
            }
            k->tbp = (256 + rnd(4) * 16) * 32;
        }
        k->tcc = rnd(2);
        k->tfx = rnd(4);
        k->cpsm = one_in(5) ? (rnd(2) ? 0xA : 0x2) : 0;
        k->csa = k->tpsm == GSN_PSMT4 ? (k->cpsm ? rnd(32) : rnd(16)) : 0;
        k->cbp = (400 + rnd(100)) * 32 + rnd(4) * 4;
        static const uint32_t clds[] = {1, 1, 1, 2, 3, 4, 5, 0};
        k->cld = clds[rnd(8)];
        k->mmag = rnd(2);
        k->mmin = rnd(2);
        k->wms = rnd(2);
        k->wmt = rnd(2);
        k->ta0 = one_in(2) ? 0x80 : rnd(256);
        k->aem = rnd(2);
        k->ta1 = one_in(2) ? 0x80 : rnd(256);
        k->fst = k->feedback ? 1 : rnd(2);
    }

    /* TEST */
    k->ate = rnd(2);
    k->atst = rnd(8);
    k->aref = one_in(3) ? 0x80 : rnd(256);
    k->afail = rnd(4);
    k->date = one_in(5) && k->fpsm != GSN_PSMCT24;
    k->datm = rnd(2);
    k->ztst = one_in(30) ? 0 : 1 + rnd(3);

    /* ALPHA */
    k->a = rnd(3);
    k->b = rnd(3);
    k->c = rnd(3);
    k->d = rnd(3);
    k->fix = one_in(4) ? 0x80 : rnd(256);
    k->colclamp = !one_in(4);
    k->fba = one_in(4);
    k->pabe = one_in(5);
    if (k->a == k->b && k->d == 1) {
        /* paraLLEl-GS makes (A - A) C + Cd an RGB write mask before PABE: the pixels PABE leaves
           unblended (As under 0x80) keep Cd there, where the GS (and the native GS) writes Cs */
        k->pabe = 0;
    }
    k->scanmsk = one_in(6) ? 2 + rnd(2) : 0;

    /* the scissor: mostly the whole buffer */
    if (one_in(3)) {
        k->sx0 = rnd(k->width);
        k->sx1 = k->sx0 + rnd(k->width - k->sx0);
        k->sy0 = rnd(k->height);
        k->sy1 = k->sy0 + rnd(k->height - k->sy0);
    } else {
        k->sx0 = 0;
        k->sx1 = k->width - 1;
        k->sy0 = 0;
        k->sy1 = k->height - 1;
    }
    k->ofx = (2048 - 320 + rnd(64)) * 16 + (one_in(3) ? rnd(16) : 0);
    k->ofy = (2048 - 224 + rnd(64)) * 16 + (one_in(3) ? rnd(16) : 0);
    k->fogcol = (uint32_t)rng() & 0xFFFFFF;

    /* the vertices */
    k->nprims = 1 + rnd(k->feedback ? 1 : 8);
    if (off("multi")) {
        k->nprims = 1;
    }
    k->nverts = k->prim == 6 ? k->nprims * 2 : k->prim == 3 ? k->nprims * 3 : k->nprims + 2;
    float tex_w = (float)(1u << k->tw), tex_h = (float)(1u << k->th);
    float margin = 16.0f;
    int whole = one_in(3) || off("frac"); /* vertices on whole pixels */
    if (off("frac")) {
        k->ofx &= ~15u;
        k->ofy &= ~15u;
    }
    k->unit_uv = k->prim == 6 && k->tme && k->fst && one_in(2);
    for (uint32_t i = 0; i < k->nverts; i++) {
        Vert *v = &k->v[i];
        v->x = sixteenths(rndf(-margin, (float)k->width + margin));
        v->y = sixteenths(rndf(-margin, (float)k->height + margin));
        if (whole) {
            v->x = floorf(v->x);
            v->y = floorf(v->y);
        }
        v->z = random_z(k);
        v->rgba = random_colour();
        v->fog = one_in(3) ? 0xFF : rnd(256);
        /* texture coordinates: across the texture and a little beyond */
        float tu = rndf(-tex_w * 0.5f, tex_w * 1.5f), tv = rndf(-tex_h * 0.5f, tex_h * 1.5f);
        if (k->feedback || one_in(3)) {
            /* about a texel a pixel, a little off */
            tu = v->x + rndf(-8.0f, 8.0f);
            tv = v->y + rndf(-8.0f, 8.0f);
        }
        if (tu < 0) {
            tu = k->fst ? 0 : tu;
        }
        if (tv < 0) {
            tv = k->fst ? 0 : tv;
        }
        v->u = (uint32_t)(tu * 16.0f) & 0x3FFF;
        v->v = (uint32_t)(tv * 16.0f) & 0x3FFF;
        v->q = one_in(3) ? 1.0f : rndf(0.5f, 2.0f);
        v->s = tu / tex_w * v->q;
        v->t = tv / tex_h * v->q;
    }
    if (k->prim == 6) {
        /* sprites: mostly the same Z, colour and Q at both corners (the second vertex's count) */
        for (uint32_t p = 0; p < k->nprims; p++) {
            Vert *a = &k->v[p * 2], *b = &k->v[p * 2 + 1];
            if (!one_in(4)) {
                a->q = b->q;
                a->s = a->s / a->q * b->q;
            }
            /* mostly drawn left to right and top to bottom */
            if (!one_in(6) || !getenv("GSN_REVERSE")) {
                if (a->x > b->x) {
                    float t = a->x;
                    a->x = b->x;
                    b->x = t;
                }
                if (a->y > b->y) {
                    float t = a->y;
                    a->y = b->y;
                    b->y = t;
                }
            }
            if (getenv("GSN_POT") && b->x > a->x && b->y > a->y) {
                /* (debugging) sprites a power of two across and down */
                b->x = a->x + (float)(1u << ilog2((uint32_t)(b->x - a->x) + 1)) / 2;
                b->y = a->y + (float)(1u << ilog2((uint32_t)(b->y - a->y) + 1)) / 2;
            }
            if (k->feedback) {
                /* the frame shifted by a few pixels */
                int dx = (int)rnd(17) - 8, dy = (int)rnd(17) - 8;
                /* read within the frame buffer: past it is VRAM the case neither sets nor compares, and
                   the native GS's target ends at the scissor (the game's passes read inside the frame) */
                a->x = fmaxf(a->x, 9.0f);
                a->y = fmaxf(a->y, 9.0f);
                b->x = fminf(b->x, (float)k->width - 9.0f);
                b->y = fminf(b->y, (float)k->height - 9.0f);
                /* and the whole buffer drawn: where a scissor cuts it, the pixels on the cut read
                   what paraLLEl-GS's feedback emulation has there */
                k->sx0 = k->sy0 = 0;
                k->sx1 = k->width - 1;
                k->sy1 = k->height - 1;
                if (b->x < a->x) {
                    b->x = a->x;
                }
                if (b->y < a->y) {
                    b->y = a->y;
                }
                a->u = (uint32_t)((int)(a->x * 16.0f) + dx * 16) & 0x3FFF;
                a->v = (uint32_t)((int)(a->y * 16.0f) + dy * 16) & 0x3FFF;
                b->u = a->u + (uint32_t)((b->x - a->x) * 16.0f);
                b->v = a->v + (uint32_t)((b->y - a->y) * 16.0f);
                if ((int)(a->x * 16.0f) + dx * 16 < 0 || (int)(a->y * 16.0f) + dy * 16 < 0) {
                    a->u = 0;
                    a->v = 0;
                    b->u = (uint32_t)((b->x - a->x) * 16.0f);
                    b->v = (uint32_t)((b->y - a->y) * 16.0f);
                }
            } else if (k->unit_uv) {
                /* a texel a pixel: UV runs as X and Y do */
                b->u = (uint32_t)((int)a->u + (int)((b->x - a->x) * 16.0f)) & 0x3FFF;
                b->v = (uint32_t)((int)a->v + (int)((b->y - a->y) * 16.0f)) & 0x3FFF;
                if (b->x < a->x || b->y < a->y) {
                    k->unit_uv = 0;
                }
            }
        }
    }
}

/* ---- Sending a case ---------------------------------------------------------------------------- */
static uint32_t float_bits(float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

static uint8_t *data_buf;

static void send_case(const Case *k) {
    uint32_t c2 = k->ctxt;
    int zbits = k->zpsm == GSN_PSMZ32 ? 32 : k->zpsm == GSN_PSMZ24 ? 24 : 16;

    /* the buffers' pixels: random */
    fill_random(data_buf, (size_t)k->width * k->height * 4);
    upload(k->fbp * 32, k->fbw, k->fpsm, 0, 0, k->width, k->height, data_buf);
    {
        /* Z: random within the format, around the vertices' */
        uint32_t *z = (uint32_t *)data_buf;
        uint16_t *z16 = (uint16_t *)data_buf;
        for (uint32_t i = 0; i < k->width * k->height; i++) {
            uint32_t v = random_z(k);
            if (zbits == 16) {
                z16[i] = (uint16_t)v;
            } else {
                z[i] = v;
            }
        }
        if (zbits == 24) {
            /* PSMZ24 uploads take 3 bytes a pixel */
            uint8_t *p = data_buf;
            for (uint32_t i = 0; i < k->width * k->height; i++) {
                uint32_t v = z[i];
                p[i * 3] = (uint8_t)v;
                p[i * 3 + 1] = (uint8_t)(v >> 8);
                p[i * 3 + 2] = (uint8_t)(v >> 16);
            }
        }
        upload(k->zbp * 32, k->fbw, k->zpsm, 0, 0, k->width, k->height, data_buf);
    }
    if (k->tme && !k->feedback) {
        uint32_t w = 1u << k->tw, h = 1u << k->th;
        fill_random(data_buf, (size_t)w * h * 4);
        if (getenv("GSN_GRADIENT") && k->tpsm == GSN_PSMCT32) {
            /* (debugging) each texel its coordinates: R = U, G = V */
            for (uint32_t i = 0; i < w * h; i++) {
                ((uint32_t *)data_buf)[i] = (i % w) | (i / w) << 8 | 0x80000000u;
            }
        }
        upload(k->tbp, k->tbw, k->tpsm, 0, 0, w, h, data_buf);
        if (k->tpsm == GSN_PSMT8 || k->tpsm == GSN_PSMT4) {
            uint32_t cw = k->tpsm == GSN_PSMT8 ? 16 : 8, ch = k->tpsm == GSN_PSMT8 ? 16 : 2;
            /* CLD 4 and 5 load only when CBP changes: sometimes the same CBP as last time */
            fill_random(data_buf, cw * ch * 4);
            upload(k->cbp, 1, k->cpsm ? k->cpsm : GSN_PSMCT32, 0, 0, cw, ch, data_buf);
        }
    }

    /* the registers */
    ad(R_TEXFLUSH, 0);
    ad(R_PRMODECONT, 1);
    ad(R_DTHE, 0);
    ad(R_FRAME_1 + c2, (uint64_t)k->fbp | (uint64_t)k->fbw << 16 | (uint64_t)k->fpsm << 24 | (uint64_t)k->fbmsk << 32);
    ad(R_ZBUF_1 + c2, (uint64_t)k->zbp | (uint64_t)(k->zpsm & 0xF) << 24 | (uint64_t)k->zmsk << 32);
    ad(R_TEST_1 + c2, (uint64_t)k->ate | k->atst << 1 | k->aref << 4 | k->afail << 12 | k->date << 14 | k->datm << 15 |
                          1u << 16 | k->ztst << 17);
    ad(R_ALPHA_1 + c2, (uint64_t)(k->a | k->b << 2 | k->c << 4 | k->d << 6) | (uint64_t)k->fix << 32);
    ad(R_SCISSOR_1 + c2, (uint64_t)k->sx0 | (uint64_t)k->sx1 << 16 | (uint64_t)k->sy0 << 32 | (uint64_t)k->sy1 << 48);
    ad(R_XYOFFSET_1 + c2, (uint64_t)k->ofx | (uint64_t)k->ofy << 32);
    ad(R_FBA_1 + c2, k->fba);
    ad(R_COLCLAMP, k->colclamp);
    ad(R_PABE, k->pabe);
    ad(R_SCANMSK, k->scanmsk);
    ad(R_FOGCOL, k->fogcol);
    ad(R_TEXA, (uint64_t)k->ta0 | (uint64_t)k->aem << 15 | (uint64_t)k->ta1 << 32);
    if (k->tme) {
        uint32_t maxu = (1u << k->tw) - 1, maxv = (1u << k->th) - 1;
        ad(R_TEX1_1 + c2, (uint64_t)1 /* LCM */ | (uint64_t)k->mmag << 5 | (uint64_t)k->mmin << 6);
        ad(R_CLAMP_1 + c2, (uint64_t)k->wms | (uint64_t)k->wmt << 2 | (uint64_t)maxu << 14 | (uint64_t)maxv << 34);
        ad(R_TEX0_1 + c2, (uint64_t)k->tbp | (uint64_t)k->tbw << 14 | (uint64_t)k->tpsm << 20 | (uint64_t)k->tw << 26 |
                              (uint64_t)k->th << 30 | (uint64_t)k->tcc << 34 | (uint64_t)k->tfx << 35 |
                              (uint64_t)k->cbp << 37 | (uint64_t)k->cpsm << 51 | (uint64_t)k->csa << 56 |
                              (uint64_t)k->cld << 61);
    }
    uint32_t prim = k->prim | k->iip << 3 | k->tme << 4 | k->fge << 5 | k->abe << 6 | k->fst << 8 | k->ctxt << 9;
    ad(R_PRIM, prim);
    put_ad(0);

    /* the vertices */
    int fog_reg = k->fge || zbits < 32;
    for (uint32_t i = 0; i < k->nverts; i++) {
        const Vert *v = &k->v[i];
        uint64_t x = (uint64_t)((int32_t)k->ofx + (int32_t)(v->x * 16.0f)) & 0xFFFF;
        uint64_t y = (uint64_t)((int32_t)k->ofy + (int32_t)(v->y * 16.0f)) & 0xFFFF;
        uint32_t z = v->z;
        if (fog_reg) {
            z &= 0xFFFFFF;
        }
        if (k->packed) {
            /* PACKED: ST (its Q goes with the RGBAQ after), RGBAQ, UV, XYZF2 or XYZ2 */
            put_qword(gif_tag(1, 0, 0, 4), 0x2 | 0x1 << 4 | 0x3 << 8 | (fog_reg ? 0x4 : 0x5) << 12);
            put_qword((uint64_t)float_bits(v->s) | (uint64_t)float_bits(v->t) << 32, float_bits(v->q));
            put_qword((uint64_t)(v->rgba & 0xFF) | (uint64_t)((v->rgba >> 8) & 0xFF) << 32,
                      (uint64_t)((v->rgba >> 16) & 0xFF) | (uint64_t)(v->rgba >> 24) << 32);
            put_qword((uint64_t)v->u | (uint64_t)v->v << 32, 0);
            if (fog_reg) {
                put_qword(x | y << 32, (uint64_t)z << 4 | (uint64_t)v->fog << 36);
            } else {
                put_qword(x | y << 32, z);
            }
        } else {
            ad(R_RGBAQ, (uint64_t)v->rgba | (uint64_t)float_bits(v->q) << 32);
            ad(R_ST, (uint64_t)float_bits(v->s) | (uint64_t)float_bits(v->t) << 32);
            ad(R_UV, (uint64_t)v->u | (uint64_t)v->v << 16);
            if (fog_reg) {
                ad(R_XYZF2, x | y << 16 | (uint64_t)z << 32 | (uint64_t)v->fog << 56);
            } else {
                ad(R_XYZ2, x | y << 16 | (uint64_t)z << 32);
            }
            put_ad(0);
        }
    }
    put_qword(gif_tag(0, 1, 0, 1), 0xE); /* the end of the packet */
    send_both(k->path);
}

/* ---- Coverage: which pixels a case's primitives cover, and which are on a triangle's edge ---------- */
static uint8_t *covered, *edge;

static void mark_triangle(const Case *k, const Vert *a, const Vert *b, const Vert *c) {
    float ax = a->x, ay = a->y, bx = b->x, by = b->y, cx = c->x, cy = c->y;
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    int x0 = (int)floorf(fminf(ax, fminf(bx, cx))) - 2, x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cx))) + 2;
    int y0 = (int)floorf(fminf(ay, fminf(by, cy))) - 2, y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cy))) + 2;
    float ex[3][3]; /* edge i: A x + B y + C, positive inside, distance in pixels */
    const float *p[3][2] = {{&ax, &ay}, {&bx, &by}, {&cx, &cy}};

    for (int i = 0; i < 3; i++) {
        float px = *p[i][0], py = *p[i][1], qx = *p[(i + 1) % 3][0], qy = *p[(i + 1) % 3][1];
        float len = sqrtf((qx - px) * (qx - px) + (qy - py) * (qy - py));
        float sgn = area >= 0 ? 1.0f : -1.0f;
        if (len == 0) {
            len = 1;
        }
        ex[i][0] = -(qy - py) / len * sgn;
        ex[i][1] = (qx - px) / len * sgn;
        ex[i][2] = -(ex[i][0] * px + ex[i][1] * py);
    }
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < (int)k->height; y++) {
        for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < (int)k->width; x++) {
            float m = 1e9f;
            for (int i = 0; i < 3; i++) {
                float d = ex[i][0] * (float)x + ex[i][1] * (float)y + ex[i][2];
                m = d < m ? d : m;
            }
            size_t at = (size_t)y * k->width + (size_t)x;
            if (fabsf(area) < 1.0f || (m > -1.0f && m < 1.0f)) {
                if (m > -1.0f) {
                    edge[at] = 1;
                    covered[at] = 1;
                }
            } else if (m >= 1.0f) {
                covered[at] = 1;
            }
        }
    }
}

static void mark_coverage(const Case *k) {
    size_t n = (size_t)k->width * k->height;

    memset(covered, 0, n);
    memset(edge, 0, n);
    if (k->prim == 6) {
        for (uint32_t p = 0; p < k->nprims; p++) {
            const Vert *a = &k->v[p * 2], *b = &k->v[p * 2 + 1];
            float x0 = fminf(a->x, b->x), x1 = fmaxf(a->x, b->x), y0 = fminf(a->y, b->y), y1 = fmaxf(a->y, b->y);
            for (int y = (int)ceilf(y0); y < (int)ceilf(y1); y++) {
                for (int x = (int)ceilf(x0); x < (int)ceilf(x1); x++) {
                    if (x >= 0 && y >= 0 && x < (int)k->width && y < (int)k->height) {
                        covered[(size_t)y * k->width + (size_t)x] = 1;
                    }
                }
            }
        }
        return;
    }
    for (uint32_t p = 0; p < k->nprims; p++) {
        if (k->prim == 3) {
            mark_triangle(k, &k->v[p * 3], &k->v[p * 3 + 1], &k->v[p * 3 + 2]);
        } else if (k->prim == 4) {
            mark_triangle(k, &k->v[p], &k->v[p + 1], &k->v[p + 2]);
        } else {
            mark_triangle(k, &k->v[0], &k->v[p + 1], &k->v[p + 2]);
        }
    }
}

/* ---- Comparing --------------------------------------------------------------------------------- */
static uint32_t *fb_a, *fb_b, *z_a, *z_b;
static int failures;
static uint32_t threshold = 1; /* a pixel differs when a channel is more than this apart (GSN_THRESHOLD) */

static void dump_ppm(const char *name, const uint32_t *rgba, uint32_t w, uint32_t h) {
    FILE *f = fopen(name, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t px[3] = {(uint8_t)rgba[i], (uint8_t)(rgba[i] >> 8), (uint8_t)(rgba[i] >> 16)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    printf("  wrote %s\n", name);
}

/* How far apart two pixels of the frame buffer are: the largest channel's difference (in the
   format's units: a CT16 buffer's 5 bits) */
static uint32_t pixel_diff(const Case *k, uint32_t p, uint32_t q) {
    uint32_t worst = 0;
    int channels = k->fpsm == GSN_PSMCT24 ? 3 : 4;
    int dst16 = k->fpsm == GSN_PSMCT16 || k->fpsm == GSN_PSMCT16S;
    for (int c = 0; c < channels; c++) {
        int e = (int)((p >> (c * 8)) & 0xFF) - (int)((q >> (c * 8)) & 0xFF);
        uint32_t m = (uint32_t)(e < 0 ? -e : e);
        if (k->abe && !k->colclamp && c < 3 && m > 128) {
            m = 256 - m; /* COLCLAMP 0 wraps: 1 apart either side of 256 is 1 apart */
        }
        if (dst16) {
            m = c == 3 ? (m ? 1 : 0) : m >> 3;
        }
        worst = m > worst ? m : worst;
    }
    return worst;
}

/* Whether two Z are apart by more than the format and the primitive allow */
static int z_differs(const Case *k, uint32_t p, uint32_t q) {
    uint32_t e = p > q ? p - q : q - p;
    uint32_t tol = k->prim == 6 ? 0 : 2;
    uint32_t big = p > q ? p : q;
    if (k->zpsm == GSN_PSMZ32 && big >= (1u << 24)) {
        tol += big >> (k->prim == 6 ? 23 : 16); /* a D32 float holds 24 bits (interpolated: less) */
    }
    if (k->prim != 6) {
        tol += big >> 20; /* interpolated in floats */
    }
    return e > tol;
}

static int run_case(int index, const Case *k) {
    GsnRect fr = {k->fbp * 32, k->fbw, k->fpsm, 0, 0, k->width, k->height};
    GsnRect zr = {k->zbp * 32, k->fbw, k->zpsm, 0, 0, k->width, k->height};
    size_t n = (size_t)k->width * k->height;
    uint32_t bad = 0, max = 0, zbad = 0, cover = 0, edges = 0, bad_any = 0, max_any = 0;
    int first_x = -1, first_y = -1;
    const uint8_t *vram;

    send_case(k);
    mark_coverage(k);

    parallel_refresh(fr.bp, fr.bw, fr.psm, fr.w, fr.h);
    parallel_refresh(zr.bp, zr.bw, zr.psm, zr.w, zr.h);
    vram = port_gs_backend_vram();
    gsn_decode_rect(vram, &fr, fb_a);
    gsn_decode_rect(vram, &zr, z_a);
    vram = gsn_vram();
    gsn_decode_rect(vram, &fr, fb_b);
    gsn_decode_rect(vram, &zr, z_b);

    for (size_t i = 0; i < n; i++) {
        uint32_t d = pixel_diff(k, fb_a[i], fb_b[i]);
        int zd = z_differs(k, z_a[i], z_b[i]);
        cover += covered[i];
        edges += edge[i];
        bad_any += d > threshold;
        max_any = d > max_any ? d : max_any;
        if (edge[i]) {
            continue;
        }
        if (d > threshold) {
            if (!bad) {
                first_x = (int)(i % k->width);
                first_y = (int)(i / k->width);
            }
            bad++;
        }
        max = d > max ? d : max;
        zbad += zd;
    }

    if (getenv("GSN_GRADIENT") && k->tme && k->tpsm == GSN_PSMCT32 && k->tfx == 1 && !k->fbmsk) {
        /* (debugging) how far apart the texels each sampled are: native less paraLLEl, wrapped */
        int hist[2][5] = {{0}};
        int tw = 1 << k->tw, th = 1 << k->th;
        for (size_t i = 0; i < n; i++) {
            int du = (int)(fb_b[i] & 0xFF) - (int)(fb_a[i] & 0xFF), dv = (int)((fb_b[i] >> 8) & 0xFF) - (int)((fb_a[i] >> 8) & 0xFF);
            if (!covered[i] || edge[i] || fb_a[i] == fb_b[i]) {
                continue;
            }
            du = ((du % tw) + tw + tw / 2) % tw - tw / 2;
            dv = ((dv % th) + th + th / 2) % th - th / 2;
            hist[0][du < -1 ? 0 : du > 1 ? 4 : du + 2]++;
            hist[1][dv < -1 ? 0 : dv > 1 ? 4 : dv + 2]++;
        }
        printf("  texel differences, native less paraLLEl: U <-1 %d, -1 %d, 0 %d, 1 %d, >1 %d; V %d %d %d %d %d\n",
               hist[0][0], hist[0][1], hist[0][2], hist[0][3], hist[0][4], hist[1][0], hist[1][1], hist[1][2], hist[1][3], hist[1][4]);
    }
    /* the criterion (the header comment) */
    int exact = k->prim == 6 && (!k->tme || (k->unit_uv && !k->feedback) || (k->feedback && k->fst && !k->mmag));
    uint32_t allowed = exact ? 0 : cover / 50 + 4;
    if (k->tme && !k->fst && (k->mmag || k->mmin)) {
        allowed = cover / 8 + 4; /* STQ and bilinear: each sixteenth of a texel is a boundary */
    }
    int ok = bad <= allowed && zbad <= allowed;
    if (getenv("GSN_ALL") || !ok) {
        printf("%s case %d (seed %u): %u pixels differ by more than 1 (of %u covered, %u on edges, %u allowed), "
               "at most %u, first at (%d,%d); Z: %u differ; all pixels: %u, at most %u\n",
               ok ? "ok" : "FAIL", index, k->seed, bad, cover, edges, allowed, max, first_x, first_y, zbad, bad_any,
               max_any);
        print_case(k);
        if (!ok && first_x >= 0) {
            size_t at = (size_t)first_y * k->width + (size_t)first_x;
            printf("  at (%d,%d): paraLLEl %08X native %08X; Z paraLLEl %08X native %08X\n", first_x, first_y,
                   fb_a[at], fb_b[at], z_a[at], z_b[at]);
        }
        if (!ok && getenv("GSN_DUMP")) {
            char name[64];
            uint32_t *diff = malloc(n * 4);
            snprintf(name, sizeof(name), "case%d_parallel.ppm", index);
            dump_ppm(name, fb_a, k->width, k->height);
            snprintf(name, sizeof(name), "case%d_native.ppm", index);
            dump_ppm(name, fb_b, k->width, k->height);
            for (size_t i = 0; i < n; i++) {
                uint32_t d = pixel_diff(k, fb_a[i], fb_b[i]);
                diff[i] = edge[i] ? 0x404040 : d > threshold ? 0x0000FF : covered[i] ? 0x00FF00 : 0;
            }
            snprintf(name, sizeof(name), "case%d_diff.ppm", index);
            dump_ppm(name, diff, k->width, k->height);
            free(diff);
        }
    }
    return ok;
}

/* ---- Buffers that share memory ----------------------------------------------------------------
   As the scrolling textures on map 000 do: 128-wide CT32 buffers a page row (32 lines) or more apart,
   drawn into, uploaded into (in their own shape and in others: T8 and T4 pages over them), copied
   between and read as textures, in a random order. The memory under them must then be the same in
   both, byte for byte: the draws are flat sprites and textured sprites a texel a pixel, which both
   draw exactly. This is what the native GS's targets that grow upwards, take uploads at an offset,
   keep the pages an upload does not write, and go back to VRAM by page have to get right. */
#define SHARED_PAGE 0x180  /* the memory's first page: 8 rows of two pages, 128 x 256 as CT32 */
#define SHARED_PAGES 16
#define SCRATCH_PAGE 0x100 /* a 128 x 128 CT32 buffer the textures are drawn into */
#define CLUT_PAGE 0x140

static void shared_frame(uint32_t page) {
    ad(R_TEXFLUSH, 0);
    ad(R_PRMODECONT, 1);
    ad(R_DTHE, 0);
    ad(R_FRAME_1, (uint64_t)page | (uint64_t)2 << 16);
    ad(R_ZBUF_1, (uint64_t)0x60 | (uint64_t)1 << 32); /* (not written) */
    ad(R_TEST_1, 1u << 16 | 1u << 17);               /* Z test ALWAYS */
    ad(R_SCISSOR_1, (uint64_t)127 << 16 | (uint64_t)127 << 48);
    ad(R_XYOFFSET_1, 0);
}

static void shared_sprite(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t u0, uint32_t v0) {
    ad(R_UV, (uint64_t)(u0 << 4) | (uint64_t)(v0 << 4) << 16);
    ad(R_XYZ2, (uint64_t)(x0 << 4) | (uint64_t)(y0 << 4) << 16);
    ad(R_UV, (uint64_t)((u0 + x1 - x0) << 4) | (uint64_t)((v0 + y1 - y0) << 4) << 16);
    ad(R_XYZ2, (uint64_t)(x1 << 4) | (uint64_t)(y1 << 4) << 16);
}

static void shared_end(void) {
    put_ad(0);
    put_qword(gif_tag(0, 1, 0, 1), 0xE);
    send_both(2);
}

/* a rectangle of 1..128 x 1..128 within the 128 x 128 a buffer is drawn in */
static void shared_rect(uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1) {
    *x0 = rnd(128);
    *y0 = rnd(128);
    *x1 = *x0 + 1 + rnd(128 - *x0);
    *y1 = *y0 + 1 + rnd(128 - *y0);
}

static int test_shared_memory(int rounds, uint32_t seed) {
    static const char *names[] = {"draw", "upload", "pages", "read", "copy"};
    int bad = 0;
    uint32_t sx0, sy0, sx1, sy1;
    int only = getenv("GSN_ROUND") ? atoi(getenv("GSN_ROUND")) : -1;
    /* GSN_SKIP=n,m: those operations of the round left out (to narrow a difference down) */
    const char *skip = getenv("GSN_SKIP");

    for (int round = 0; round < rounds; round++) {
        char ops[512] = "";
        /* each round from its own seed, and from memory made the same in both */
        rng_state = (0x5851F42D4C957F2Dull ^ ((uint64_t)seed << 32 | (uint32_t)round)) * 0xD1B54A32D192ED03ull | 1;
        for (int w = 0; w < 4; w++) {
            rng();
        }
        if (only >= 0 && round != only) {
            continue;
        }
        fill_random(data_buf, 128 * 256 * 4);
        upload(SHARED_PAGE * 32, 2, GSN_PSMCT32, 0, 0, 128, 256, data_buf);
        fill_random(data_buf, 128 * 128 * 4);
        upload(SCRATCH_PAGE * 32, 2, GSN_PSMCT32, 0, 0, 128, 128, data_buf);
        fill_random(data_buf, 16 * 16 * 4);
        upload(CLUT_PAGE * 32, 1, GSN_PSMCT32, 0, 0, 16, 16, data_buf);
        int nops = 2 + (int)rnd(14);
        for (int o = 0; o < nops; o++) {
            uint32_t op = rnd(5), row = rnd(5); /* the buffer: this many page rows into the memory */
            discard = 0;
            if (skip) {
                char tag[16];
                snprintf(tag, sizeof(tag), ",%d,", o);
                char list[256];
                snprintf(list, sizeof(list), ",%s,", skip);
                discard = strstr(list, tag) != NULL; /* (made, so that the rest are as they were, but not sent) */
            }
            uint32_t page = SHARED_PAGE + row * 2;
            char one[48];
            snprintf(one, sizeof(one), "%s%s%s %u", o ? ", " : "", discard ? "-" : "", names[op], row);
            if (strlen(ops) + strlen(one) < sizeof(ops)) {
                strcat(ops, one);
            }
            switch (op) {
            case 0: /* flat sprites */
                shared_frame(page);
                ad(R_PRIM, 6);
                for (uint32_t n = 1 + rnd(3); n > 0; n--) {
                    shared_rect(&sx0, &sy0, &sx1, &sy1);
                    ad(R_RGBAQ, (uint64_t)(uint32_t)rng() | (uint64_t)0x3F800000 << 32);
                    shared_sprite(sx0, sy0, sx1, sy1, 0, 0);
                }
                shared_end();
                break;
            case 1: /* an upload in the buffer's own shape */
                shared_rect(&sx0, &sy0, &sx1, &sy1);
                /* (whole qwords: the padding of a last qword is left out of what the GS defines) */
                sx0 &= ~3u;
                sx1 = sx0 + 4 + ((sx1 - sx0) & ~3u);
                sx1 = sx1 > 128 ? 128 : sx1;
                fill_random(data_buf, 128 * 128 * 4);
                upload(page * 32, 2, GSN_PSMCT32, sx0, sy0, sx1 - sx0, sy1 - sy0, data_buf);
                break;
            case 2: { /* whole pages of a texture in another format over the memory */
                uint32_t t4 = rnd(2), n = 1 + rnd(2), ph = t4 ? 128 : 64;
                uint32_t first = SHARED_PAGE + rnd(SHARED_PAGES - n + 1);
                fill_random(data_buf, 128 * ph * n);
                upload(first * 32, 2, t4 ? GSN_PSMT4 : GSN_PSMT8, 0, 0, 128, ph * n, data_buf);
                break;
            }
            case 3: { /* the buffer read as a texture (CT32, or T8 through the CLUT), a texel a pixel */
                uint32_t t8 = rnd(2);
                shared_frame(SCRATCH_PAGE);
                ad(R_TEX1_1, 1);   /* LCM, nearest */
                ad(R_CLAMP_1, 0);  /* REPEAT */
                ad(R_TEX0_1, (uint64_t)(page * 32) | (uint64_t)2 << 14 | (uint64_t)(t8 ? GSN_PSMT8 : GSN_PSMCT32) << 20 |
                                 (uint64_t)7 << 26 | (uint64_t)(t8 ? 6 : 7) << 30 | (uint64_t)1 << 34 | (uint64_t)1 << 35 |
                                 (uint64_t)(CLUT_PAGE * 32) << 37 | (uint64_t)1 << 61);
                ad(R_PRIM, 6 | 1 << 4 | 1 << 8);
                ad(R_RGBAQ, 0x80808080u | (uint64_t)0x3F800000 << 32);
                shared_rect(&sx0, &sy0, &sx1, &sy1);
                shared_sprite(sx0, sy0, sx1, sy1, rnd(128), rnd(t8 ? 64 : 128));
                shared_end();
                break;
            }
            default: { /* a local-to-local copy from another buffer of the memory */
                uint32_t from_row = rnd(5), from = SHARED_PAGE + from_row * 2;
                shared_rect(&sx0, &sy0, &sx1, &sy1);
                if ((from_row > row ? from_row - row : row - from_row) * 32 < sy1 - sy0) {
                    from = page; /* (rectangles that overlap in memory: the GS's pixel order would matter) */
                }
                ad(R_BITBLTBUF, (uint64_t)(from * 32) | (uint64_t)2 << 16 | (uint64_t)(page * 32) << 32 | (uint64_t)2 << 48);
                ad(R_TRXPOS, (uint64_t)sx0 | (uint64_t)sy0 << 16 | (uint64_t)sx0 << 32 | (uint64_t)sy0 << 48);
                ad(R_TRXREG, (uint64_t)(sx1 - sx0) | (uint64_t)(sy1 - sy0) << 32);
                ad(R_TRXDIR, 2);
                shared_end();
                break;
            }
            }
        }
        discard = 0;
        parallel_refresh(SHARED_PAGE * 32, 2, GSN_PSMCT32, 128, 256);
        parallel_refresh(SCRATCH_PAGE * 32, 2, GSN_PSMCT32, 128, 128);
        const uint8_t *a = port_gs_backend_vram(), *b = gsn_vram();
        int differ = memcmp(a + SHARED_PAGE * 8192, b + SHARED_PAGE * 8192, SHARED_PAGES * 8192) != 0 ||
                     memcmp(a + SCRATCH_PAGE * 8192, b + SCRATCH_PAGE * 8192, 4 * 8192) != 0;
        if (differ || getenv("GSN_ALL")) {
            printf("%s shared memory round %d: %s\n", differ ? "FAIL" : "ok", round, ops);
        }
        if (differ) {
            bad++;
            if (getenv("GSN_VERBOSE")) {
                for (uint32_t pg = 0; pg < SHARED_PAGES; pg++) {
                    if (memcmp(a + (SHARED_PAGE + pg) * 8192, b + (SHARED_PAGE + pg) * 8192, 8192)) {
                        printf("  page %u (row %u) differs\n", pg, pg / 2);
                    }
                }
                if (memcmp(a + SCRATCH_PAGE * 8192, b + SCRATCH_PAGE * 8192, 4 * 8192)) {
                    printf("  the scratch buffer differs\n");
                }
            }
        }
    }
    return bad;
}

/* gsn_draw_test [cases [seed]] */
int main(int argc, char **argv) {
    clock_t start = clock();
    int cases = argc > 1 ? atoi(argv[1]) : 300;
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1;
    int only = getenv("GSN_CASE") ? atoi(getenv("GSN_CASE")) : -1;
    int first = getenv("GSN_FIRST") ? atoi(getenv("GSN_FIRST")) : 0; /* the cases before, left out */
    off_list = getenv("GSN_OFF");
    if (getenv("GSN_THRESHOLD")) {
        threshold = (uint32_t)atoi(getenv("GSN_THRESHOLD"));
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!port_gs_backend_open()) {
        printf("no Vulkan device for paraLLEl-GS: skipped\n");
        return 77;
    }
    if (!gsn_open(NULL)) {
        printf("no Vulkan device for the native GS: skipped\n");
        return 77;
    }
    data_buf = malloc(1024 * 1024 * 4);
    covered = malloc(256 * 128);
    edge = malloc(256 * 128);
    fb_a = malloc(256 * 128 * 4);
    fb_b = malloc(256 * 128 * 4);
    z_a = malloc(256 * 128 * 4);
    z_b = malloc(256 * 128 * 4);

    for (int i = 0; i < cases; i++) {
        Case k;
        /* each case from its own seed, so one can be run alone (GSN_CASE) */
        rng_state = (0x9E3779B97F4A7C15ull ^ ((uint64_t)seed << 32 | (uint32_t)i)) * 0xD1B54A32D192ED03ull | 1;
        for (int w = 0; w < 4; w++) {
            rng();
        }
        make_case(&k);
        k.seed = seed;
        apply_off(&k);
        if ((only >= 0 && i != only) || i < first) {
            continue;
        }
        if (!run_case(i, &k)) {
            failures++;
        }
    }
    if (only < 0 && !getenv("GSN_NO_SHARED")) {
        int bad = test_shared_memory(cases, seed);
        printf("%d of %d shared memory rounds %s\n", bad, cases, bad ? "FAILED" : "failed");
        failures += bad;
    }
    printf("%d of %d cases %s, %.1f s\n", failures, cases, failures ? "FAILED" : "failed", (double)(clock() - start) / CLOCKS_PER_SEC);
    return failures ? 1 : 0;
}
