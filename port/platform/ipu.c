/* The IPU's input: DMA channel 4, read as the IPU takes the stream (port/include/port/platform.h).

   The movie player (gfx/movie.c, the SDK's streaming sample) queues the video stream for the IPU as a
   source chain over a ring of 0x800-byte blocks: a REF tag per block, a REFE on the last block
   written, and a NEXT back to the first tag. It stops and restarts the channel as it adds blocks, and
   reads back where the channel is (D4_MADR, CHCR's TAG) to know which blocks the IPU has taken, and
   where the IPU is in them (IPU_BP) for the time stamps.

   So the channel is not the DMA controller thread's (port/platform/dmac.c leaves it alone): it moves
   only when the decoder (port/sdk/libmpeg.c) takes a quadword, on the game's thread, and its
   registers say exactly how far the IPU has read. Nothing runs ahead of the decoder, as the IPU's
   FIFO would: libmpeg.c never suspends the IPU (the stop and restart callbacks), which is when the
   FIFO's contents would matter. */
#include <string.h>

#include "eeregs.h"
#include "port/host.h"
#include "port/platform.h"

#define CHCR_STR 0x100
#define TAG_REFE 0
#define TAG_CNT 1
#define TAG_NEXT 2
#define TAG_REF 3
#define TAG_REFS 4
#define TAG_END 7
/* more tags with no data in a row than a chain has: one that loops */
#define MAX_EMPTY_TAGS 100000

static struct {
    int running;       /* the start was seen */
    int last;          /* the qwords left are the chain's last (a REFE or END tag) */
    uint32_t tag_bits; /* the upper half of the last tag read: what CHCR's TAG shows */
} channel;

/* The game wrote CHCR. A start: the qwords QWC leaves go first, and the tag CHCR holds says whether
   the chain goes on after them. A stop: CHCR's TAG keeps the tag the channel stopped on (the game
   writes the whole register, and reads TAG back to see where the channel was). */
void port_ipu_dma_sync(void) {
    uint32_t chcr = (uint32_t)*D4_CHCR;

    if (chcr & CHCR_STR) {
        if (!channel.running) {
            uint32_t id = (chcr >> 28) & 7;

            channel.running = 1;
            channel.tag_bits = chcr >> 16;
            channel.last = (*D4_QWC & 0xFFFF) != 0 && (id == TAG_REFE || id == TAG_END);
        }
    } else if (channel.running) {
        channel.running = 0;
        *D4_CHCR = (int)((chcr & 0xFFFF) | channel.tag_bits << 16);
    }
}

int port_ipu_read_qword(uint8_t qword[16]) {
    int empty = 0;

    port_ipu_dma_sync();
    if (!channel.running) {
        return 0;
    }
    for (;;) {
        uint32_t qwc = (uint32_t)*D4_QWC & 0xFFFF;
        uint32_t tadr;
        const uint32_t *tag;
        uint32_t address;

        if (qwc != 0) {
            uint32_t madr = (uint32_t)*D4_MADR;

            memcpy(qword, ee_ptr_of(madr), 16);
            *D4_MADR = (int)(madr + 16);
            *D4_QWC = (int)(qwc - 1);
            return 1;
        }
        if (channel.last) {
            /* the chain is done: STR clears */
            channel.running = 0;
            channel.last = 0;
            *D4_CHCR = (int)(((uint32_t)*D4_CHCR & 0xFFFF & ~CHCR_STR) | channel.tag_bits << 16);
            return 0;
        }
        if (++empty > MAX_EMPTY_TAGS) {
            PORT_FATAL("DMA channel 4: a chain of tags with no data (it loops?)");
        }
        tadr = (uint32_t)*D4_TADR;
        tag = ee_ptr_of(tadr);
        address = tag[1] & 0xFFFFFFF0u;
        channel.tag_bits = tag[0] >> 16;
        *D4_QWC = (int)(tag[0] & 0xFFFF);
        switch ((tag[0] >> 28) & 7) {
        case TAG_REFE:
            *D4_MADR = (int)address;
            *D4_TADR = (int)(tadr + 16);
            channel.last = 1;
            break;
        case TAG_CNT:
            *D4_MADR = (int)(tadr + 16);
            *D4_TADR = (int)(tadr + 16 + (tag[0] & 0xFFFF) * 16);
            break;
        case TAG_NEXT:
            *D4_MADR = (int)(tadr + 16);
            *D4_TADR = (int)address;
            break;
        case TAG_REF:
        case TAG_REFS:
            *D4_MADR = (int)address;
            *D4_TADR = (int)(tadr + 16);
            break;
        case TAG_END:
            *D4_MADR = (int)(tadr + 16);
            channel.last = 1;
            break;
        default:
            PORT_FATAL("DMA channel 4: tag ID %u (CALL or RET) in the stream's chain", (tag[0] >> 28) & 7);
        }
    }
}

/* IPU_BP for a decoder holding `unused_bits` bits it has read but not used: they are in the FIFO's
   first FP + IFC quadwords, from bit BP of the first. */
void port_ipu_set_bp(int unused_bits) {
    int quadwords = (unused_bits + 127) / 128;
    int fp = quadwords < 2 ? quadwords : 2;

    *IPU_BP = (quadwords * 128 - unused_bits) | (quadwords - fp) << 8 | fp << 16;
}
