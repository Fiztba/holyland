/* The tables libsd keeps in the disc's LIBSD.IRX: the ten reverb modes' work area sizes and
   register presets (sceSdSetEffectAttr), and the 16-byte block every idle voice plays (sceSdInit).
   They are read from the user's disc at start-up, so no byte of Sony's module is in this
   repository; without them the SPU2 keeps its built-in values (psx-spx's presets).

   Adapted from ico-pc's port/audio/libsd_irx.c (MIT, see third_party/ico-audio/LICENSE): KF4's
   LIBSD.IRX is the same build as ICO's (26285 bytes), with the tables at the same addresses. */
#include <stdlib.h>
#include <string.h>

#include "port/audio.h"
#include "port/platform.h"
#include "spu2.h"
#include "spu2_internal.h"
#include "spu2_sd.h"

#define LIBSD_IRX_PATH "\\IOP\\LIBSD.IRX;1"
#define LIBSD_IRX_SIZE 26285u
/* Module addresses in .data, found through the code that reads them: sceSdSetEffectAttr loads
   sizes[mode] from 0x41C8 and copies the 0x44-byte preset at 0x41F0 + mode * 0x44; InitVoices
   writes 16 halfwords from 0x4A60 to the data port */
#define LIBSD_SIZES_ADDR 0x41C8u   /* 10 x u32, the work area's size in 8-byte units */
#define LIBSD_PRESETS_ADDR 0x41F0u /* 10 x {u32 flags; u16 regs[32]} */
#define LIBSD_PRESET_BYTES 0x44u
#define LIBSD_IDLE_ADDR 0x4A60u    /* 16 bytes */

typedef struct {
    spu2_reverb_preset presets[SPU2_REVERB_MODES];
    uint8_t idle[SPU2_SD_IDLE_BLOCK_BYTES];
} LibsdValues;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

/* The file offset of `len` bytes at module address `addr` in .data, or -1 */
static long data_off(uint32_t sh_addr, uint32_t sh_off, uint32_t sh_size, uint32_t addr, uint32_t len) {
    if (addr < sh_addr || addr - sh_addr > sh_size || sh_size - (addr - sh_addr) < len) {
        return -1;
    }
    return (long)(sh_off + (addr - sh_addr));
}

/* The tables of a whole LIBSD.IRX image, with structural checks; NULL, or why not */
static const char *libsd_parse(const uint8_t *irx, size_t len, LibsdValues *out) {
    static const uint8_t elf_magic[4] = {0x7F, 'E', 'L', 'F'};
    uint32_t shoff, shentsize, shnum, shstrndx, str_off, str_size;
    uint32_t d_addr = 0, d_off = 0, d_size = 0;
    long o_sizes, o_presets, o_idle;
    uint32_t i;
    int found = 0;
    int m;

    memset(out, 0, sizeof(*out));
    if (len < 0x34 || memcmp(irx, elf_magic, 4) != 0 || irx[4] != 1 || irx[5] != 1) {
        return "not a 32-bit little-endian ELF";
    }
    shoff = le32(irx + 0x20);
    shentsize = le16(irx + 0x2E);
    shnum = le16(irx + 0x30);
    shstrndx = le16(irx + 0x32);
    if (shentsize < 0x28 || shstrndx >= shnum || shoff > len || (uint64_t)shnum * shentsize > len - shoff) {
        return "bad section headers";
    }
    str_off = le32(irx + shoff + shstrndx * shentsize + 0x10);
    str_size = le32(irx + shoff + shstrndx * shentsize + 0x14);
    if (str_off > len || str_size > len - str_off) {
        return "bad section name table";
    }
    for (i = 0; i < shnum; i++) {
        const uint8_t *sh = irx + shoff + i * shentsize;
        uint32_t name = le32(sh);

        if (name < str_size && str_size - name >= 6 && memcmp(irx + str_off + name, ".data", 6) == 0) {
            d_addr = le32(sh + 0x0C);
            d_off = le32(sh + 0x10);
            d_size = le32(sh + 0x14);
            found = 1;
            break;
        }
    }
    if (!found) {
        return "no .data section";
    }
    if (d_off > len || d_size > len - d_off) {
        return ".data runs past the file";
    }
    o_sizes = data_off(d_addr, d_off, d_size, LIBSD_SIZES_ADDR, 4u * SPU2_REVERB_MODES);
    o_presets = data_off(d_addr, d_off, d_size, LIBSD_PRESETS_ADDR, LIBSD_PRESET_BYTES * SPU2_REVERB_MODES);
    o_idle = data_off(d_addr, d_off, d_size, LIBSD_IDLE_ADDR, SPU2_SD_IDLE_BLOCK_BYTES);
    if (o_sizes < 0 || o_presets < 0 || o_idle < 0) {
        return "the tables are not inside .data";
    }
    for (m = 0; m < SPU2_REVERB_MODES; m++) {
        const uint8_t *p = irx + o_presets + (long)LIBSD_PRESET_BYTES * m;
        spu2_reverb_preset *r = &out->presets[m];
        uint32_t units = le32(irx + o_sizes + 4 * m);
        uint32_t top = 0;
        int k;

        if (units == 0 || units > (SPU2_RAM_SIZE >> 3)) {
            return "a work area size is out of range";
        }
        r->size = units << 3;
        for (k = 0; k < 32; k++) {
            r->regs[k] = le16(p + 4 + 2 * k);
        }
        for (k = 10; k < 30; k++) {
            top = r->regs[k] > top ? r->regs[k] : top;
        }
        if (le32(p) != 0) {
            return "a preset's leading word is not 0";
        }
        if ((top << 3) >= r->size) {
            return "a preset addresses past its work area";
        }
        if (m > 0 && (r->regs[30] != 0x8000 || r->regs[31] != 0x8000)) {
            return "a preset's input volumes are not 0x8000";
        }
    }
    memcpy(out->idle, irx + o_idle, SPU2_SD_IDLE_BLOCK_BYTES);
    if ((out->idle[1] & 3) != 3) {
        return "the idle block does not loop on itself";
    }
    return NULL;
}

/* After spu2_reset: installs the disc's tables, or says why the built-in ones stay */
void port_audio_load_libsd(void) {
    static LibsdValues values;
    uint32_t lsn;
    uint32_t size;
    uint8_t *image = NULL;
    const char *why = NULL;
    unsigned differ = 0;
    int m;

    if (!port_disc_find(LIBSD_IRX_PATH, &lsn, &size)) {
        why = "the disc has no \\IOP\\LIBSD.IRX";
    } else if (size != LIBSD_IRX_SIZE) {
        why = "\\IOP\\LIBSD.IRX is not the build these addresses are for (its size)";
    } else if ((image = malloc((size + DISC_SECTOR_SIZE - 1) / DISC_SECTOR_SIZE * DISC_SECTOR_SIZE)) == NULL) {
        why = "no memory";
    } else if (!port_disc_read(lsn, (size + DISC_SECTOR_SIZE - 1) / DISC_SECTOR_SIZE, image)) {
        why = "\\IOP\\LIBSD.IRX cannot be read";
    } else {
        why = libsd_parse(image, size, &values);
    }
    free(image);
    if (why != NULL) {
        port_event("libsd: %s; the reverb presets and idle block are the built-in ones (psx-spx)", why);
        return;
    }
    for (m = 0; m < SPU2_REVERB_MODES; m++) {
        const spu2_reverb_preset *built_in = spu2_reverb_get_preset(m);

        if (built_in->size != values.presets[m].size ||
            memcmp(built_in->regs, values.presets[m].regs, sizeof(built_in->regs)) != 0) {
            differ |= 1u << m;
        }
        spu2_reverb_set_preset(m, &values.presets[m]);
    }
    spu2_sd_set_idle_block(values.idle);
    port_event("libsd: reverb presets, work area sizes and idle block from the disc's LIBSD.IRX "
               "(modes that differ from psx-spx's: 0x%03X)", differ);
}
