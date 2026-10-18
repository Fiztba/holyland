/* VIF1, the interface in front of VU1: it reads the VIF codes in what DMA channel 1 sends, unpacks
   data into VU1's memory, uploads microprograms, starts VU1 (port/platform/vu1.c), and passes GIF
   packets straight on to the GS (DIRECT, PATH2). VU1's XGKICK sends a GIF packet from its memory
   (PATH1).

   The words come in as DMA delivers them (port/platform/dmac.c): a command and its data may be
   split across DMA tags, so this is fed a word at a time and keeps a command's data until it is all
   there. VU1 runs to its end inside MSCAL, so VIF1 never waits for it.

   Only the DMA controller's thread calls in here. */
#include <stdint.h>
#include <string.h>

#include "port/host.h"
#include "port/platform.h"

#define VU1_QWORDS 1024

static struct {
    uint32_t cl, wl;         /* STCYCL */
    uint32_t mode;           /* STMOD */
    uint32_t mask;           /* STMASK */
    uint32_t row[4], col[4]; /* STROW, STCOL */
    uint32_t base, ofst, tops, itops, top, itop;
    int dbf;
    int mskpath3;
    uint32_t mark;

    /* the command being read */
    uint32_t code;
    uint32_t need;  /* data words the command takes */
    uint32_t have;  /* data words so far */
    int active;
} vif;

/* the data of the command being read: at most an UNPACK of 256 V4-32 vectors, or 65536 qwords of
   DIRECT, which are passed on as they come */
static uint32_t pending[256 * 4 + 4];
#define DIRECT_CHUNK_WORDS 1024

void port_vif1_reset(void) {
    memset(&vif, 0, sizeof(vif));
}

/* ---- PATH1: XGKICK ----------------------------------------------------------------------------- */
static uint8_t path1_buffer[VU1_QWORDS * 16 * 2];

void port_gif_path1_kick(uint32_t address) {
    uint8_t *vu_data = port_vu1_data();
    uint32_t qwords = 0;
    int eop = 0;

    address &= VU1_QWORDS - 1;
    while (!eop && qwords < VU1_QWORDS * 2) {
        uint64_t tag;
        uint32_t nloop, flg, nreg, size, n;

        memcpy(&tag, vu_data + ((address + qwords) & (VU1_QWORDS - 1)) * 16, 8);
        nloop = (uint32_t)(tag & 0x7FFF);
        eop = (int)((tag >> 15) & 1);
        flg = (uint32_t)((tag >> 58) & 3);
        nreg = (uint32_t)((tag >> 60) & 15);
        if (nreg == 0) {
            nreg = 16;
        }
        if (flg == 0) {
            size = nloop * nreg;
        } else if (flg == 1) {
            size = (nloop * nreg + 1) / 2;
        } else {
            size = nloop;
        }
        for (n = 0; n <= size && qwords < VU1_QWORDS * 2; n++, qwords++) {
            memcpy(path1_buffer + qwords * 16, vu_data + ((address + qwords) & (VU1_QWORDS - 1)) * 16, 16);
        }
    }
    port_gs_gif(1, path1_buffer, (size_t)qwords * 16);
}

/* ---- UNPACK ------------------------------------------------------------------------------------ */
static uint32_t unpack_vectors_read(uint32_t num) {
    if (vif.cl >= vif.wl) {
        return num;
    }
    /* filling: only cl of every wl qwords take data */
    return vif.cl * (num / vif.wl) + (num % vif.wl > vif.cl ? vif.cl : num % vif.wl);
}

static uint32_t unpack_bits(uint32_t cmd) {
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;

    if (vl == 3) {
        return 16; /* V4-5 */
    }
    return (vn + 1) * (32u >> vl);
}

static uint32_t element(const uint8_t *bytes, uint32_t index, uint32_t vl, int usn) {
    switch (vl) {
    case 0: {
        uint32_t v;
        memcpy(&v, bytes + index * 4, 4);
        return v;
    }
    case 1: {
        uint16_t v;
        memcpy(&v, bytes + index * 2, 2);
        return usn ? v : (uint32_t)(int32_t)(int16_t)v;
    }
    default: {
        uint8_t v = bytes[index];
        return usn ? v : (uint32_t)(int32_t)(int8_t)v;
    }
    }
}

static void do_unpack(void) {
    uint32_t cmd = (vif.code >> 24) & 0x7F;
    uint32_t num = (vif.code >> 16) & 0xFF;
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    int masked = (cmd & 0x10) != 0;
    int usn = (vif.code >> 14) & 1;
    uint32_t address = vif.code & 0x3FF;
    const uint8_t *bytes = (const uint8_t *)pending;
    uint8_t *vu_data = port_vu1_data();
    uint32_t written, read = 0;
    uint32_t cl = vif.cl, wl = vif.wl;

    if (num == 0) {
        num = 256;
    }
    if ((vif.code >> 15) & 1) {
        address += vif.tops;
    }
    if (wl == 0) {
        PORT_TODO("VIF1 UNPACK with WL 0");
        return;
    }
    for (written = 0; written < num; written++) {
        uint32_t block = written / wl, in_block = written % wl;
        uint32_t qaddr = cl >= wl ? address + block * cl + in_block : address + written;
        uint32_t *dest = (uint32_t *)(vu_data + (qaddr & (VU1_QWORDS - 1)) * 16);
        uint32_t row_index = in_block < 3 ? in_block : 3;
        int has_data = cl >= wl || in_block < cl;
        uint32_t value[4];
        int f;

        if (has_data) {
            uint32_t i = read++;

            if (vl == 3) {
                uint16_t v;

                memcpy(&v, bytes + i * 2, 2);
                value[0] = (uint32_t)(v & 0x1F) << 3;
                value[1] = (uint32_t)((v >> 5) & 0x1F) << 3;
                value[2] = (uint32_t)((v >> 10) & 0x1F) << 3;
                value[3] = (uint32_t)((v >> 15) & 1) << 7;
            } else if (vn == 0) {
                value[0] = value[1] = value[2] = value[3] = element(bytes, i, vl, usn);
            } else if (vn == 1) {
                value[0] = value[2] = element(bytes, i * 2, vl, usn);
                value[1] = value[3] = element(bytes, i * 2 + 1, vl, usn);
            } else if (vn == 2) {
                /* V3: w takes the element after the vector, as the hardware does */
                value[0] = element(bytes, i * 3, vl, usn);
                value[1] = element(bytes, i * 3 + 1, vl, usn);
                value[2] = element(bytes, i * 3 + 2, vl, usn);
                value[3] = element(bytes, i * 3 + 3, vl, usn);
            } else {
                value[0] = element(bytes, i * 4, vl, usn);
                value[1] = element(bytes, i * 4 + 1, vl, usn);
                value[2] = element(bytes, i * 4 + 2, vl, usn);
                value[3] = element(bytes, i * 4 + 3, vl, usn);
            }
        } else {
            PORT_TODO("VIF1 UNPACK in filling mode");
            value[0] = value[1] = value[2] = value[3] = 0;
        }
        for (f = 0; f < 4; f++) {
            uint32_t m = masked ? (vif.mask >> (row_index * 8 + f * 2)) & 3 : 0;

            if (!has_data && m == 0) {
                m = 1;
            }
            switch (m) {
            case 0:
                if (vif.mode == 1) {
                    dest[f] = value[f] + vif.row[f];
                } else if (vif.mode == 2) {
                    vif.row[f] += value[f];
                    dest[f] = vif.row[f];
                } else {
                    dest[f] = value[f];
                }
                break;
            case 1:
                dest[f] = vif.row[f];
                break;
            case 2:
                dest[f] = vif.col[row_index];
                break;
            default:
                break; /* write-protected */
            }
        }
    }
}

/* ---- Commands ------------------------------------------------------------------------------- */
static void start_vu1(int cont) {
    vif.top = vif.tops;
    vif.itop = vif.itops;
    if (vif.dbf) {
        vif.dbf = 0;
        vif.tops = vif.base;
    } else {
        vif.dbf = 1;
        vif.tops = (vif.base + vif.ofst) & 0x3FF;
    }
    if (cont) {
        port_vu1_continue(vif.top, vif.itop);
    } else {
        port_vu1_start(vif.code & 0xFFFF, vif.top, vif.itop);
    }
}

/* Starts a command; 1 if it takes data words. */
static int begin(uint32_t code) {
    uint32_t cmd = (code >> 24) & 0x7F;
    uint32_t num = (code >> 16) & 0xFF;
    uint32_t imm = code & 0xFFFF;

    vif.code = code;
    vif.have = 0;
    if (cmd >= 0x60) {
        uint32_t n = num ? num : 256;
        uint32_t bits = unpack_vectors_read(n) * unpack_bits(cmd);

        vif.need = (bits + 31) / 32;
        return vif.need != 0;
    }
    switch (cmd) {
    case 0x00: /* NOP */
        return 0;
    case 0x01: /* STCYCL */
        vif.cl = imm & 0xFF;
        vif.wl = (imm >> 8) & 0xFF;
        return 0;
    case 0x02: /* OFFSET */
        vif.ofst = imm & 0x3FF;
        vif.dbf = 0;
        vif.tops = vif.base;
        return 0;
    case 0x03: /* BASE */
        vif.base = imm & 0x3FF;
        return 0;
    case 0x04: /* ITOP */
        vif.itops = imm & 0x3FF;
        return 0;
    case 0x05: /* STMOD */
        vif.mode = imm & 3;
        return 0;
    case 0x06: /* MSKPATH3 */
        vif.mskpath3 = (imm >> 15) & 1;
        return 0;
    case 0x07: /* MARK */
        vif.mark = imm;
        return 0;
    case 0x10: /* FLUSHE */
    case 0x11: /* FLUSH */
    case 0x13: /* FLUSHA */
        return 0;
    case 0x14: /* MSCAL */
    case 0x15: /* MSCALF */
        start_vu1(0);
        return 0;
    case 0x17: /* MSCNT */
        start_vu1(1);
        return 0;
    case 0x20: /* STMASK */
        vif.need = 1;
        return 1;
    case 0x30: /* STROW */
    case 0x31: /* STCOL */
        vif.need = 4;
        return 1;
    case 0x4A: /* MPG */
        vif.need = (num ? num : 256) * 2;
        return 1;
    case 0x50: /* DIRECT */
    case 0x51: /* DIRECTHL */
        vif.need = (imm ? imm : 0x10000) * 4;
        return 1;
    }
    PORT_TODO("VIF1: a VIF code it does not know");
    return 0;
}

static void finish(void) {
    uint32_t cmd = (vif.code >> 24) & 0x7F;

    if (cmd >= 0x60) {
        do_unpack();
        return;
    }
    switch (cmd) {
    case 0x20:
        vif.mask = pending[0];
        break;
    case 0x30:
        memcpy(vif.row, pending, 16);
        break;
    case 0x31:
        memcpy(vif.col, pending, 16);
        break;
    case 0x4A: {
        uint32_t address = (vif.code & 0xFFFF) * 8;
        uint32_t bytes = vif.need * 4;
        uint8_t *micro = port_vu1_micro();
        uint32_t n;

        for (n = 0; n < bytes; n++) {
            micro[(address + n) & 0x3FFF] = ((const uint8_t *)pending)[n];
        }
        port_vu1_micro_written();
        break;
    }
    }
}

void port_vif1_feed(const uint32_t *words, size_t count) {
    size_t i = 0;

    while (i < count) {
        if (!vif.active) {
            uint32_t code = words[i++];

            if (begin(code)) {
                vif.active = 1;
            }
            continue;
        }
        {
            uint32_t cmd = (vif.code >> 24) & 0x7F;
            size_t take = vif.need - vif.have;

            if (take > count - i) {
                take = count - i;
            }
            if (cmd == 0x50 || cmd == 0x51) {
                /* DIRECT goes on to the GS as it comes, in whole quadwords */
                size_t sent = 0;

                while (sent < take) {
                    size_t chunk = take - sent;
                    uint32_t fill = vif.have % 4;

                    if (fill != 0 || chunk < 4) {
                        /* a quadword split across DMA tags: gather it */
                        size_t room = 4 - fill;
                        size_t n = chunk < room ? chunk : room;

                        memcpy(pending + fill, words + i + sent, n * 4);
                        vif.have += (uint32_t)n;
                        sent += n;
                        if (vif.have % 4 == 0) {
                            port_gs_gif(2, pending, 16);
                        }
                        continue;
                    }
                    chunk &= ~(size_t)3;
                    if (chunk > DIRECT_CHUNK_WORDS) {
                        chunk = DIRECT_CHUNK_WORDS;
                    }
                    port_gs_gif(2, words + i + sent, chunk * 4);
                    vif.have += (uint32_t)chunk;
                    sent += chunk;
                }
                i += take;
            } else {
                memcpy(pending + vif.have, words + i, take * 4);
                vif.have += (uint32_t)take;
                i += take;
            }
            if (vif.have == vif.need) {
                vif.active = 0;
                if (cmd != 0x50 && cmd != 0x51) {
                    finish();
                }
            }
        }
    }
}

int port_vif1_path3_masked(void) {
    return vif.mskpath3;
}
