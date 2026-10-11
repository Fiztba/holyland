// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr uint32_t gif = 0x1000a000u, head = 0x10000u, payload = 0x20000u;
int checks = 0, failures = 0;
void check(bool pass, const char *message) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void finish(PS2Memory &m, uint32_t at) {
    m.write64(at, (1ull << 60) | 0x8001u);
    m.write64(at + 8, 0x0eu);
    m.write64(at + 16, 0);
    m.write64(at + 24, 0x61u);
}
void tag(PS2Memory &m, uint32_t at, uint32_t id, uint32_t qwc) {
    m.write64(at, (uint64_t(id) << 28) | qwc);
    m.write64(at + 8, 0);
}
void run(uint32_t id, bool scratch, bool irq, bool tie, bool empty = false) {
    PS2Memory m;
    check(m.initialize(), "memory initialized");
    GS gs;
    gs.init(m.getGSVRAM(), PS2_GS_VRAM_SIZE, &m.gs());
    std::vector<uint8_t> delivered;
    m.setGifPacketCallback([&](const uint8_t *data, uint32_t size) {
        delivered.insert(delivered.end(), data, data + size);
        gs.processGIFPacket(data, size);
    });
    m.write64(0x12001000u, 2u);
    const uint32_t cpuAddress = scratch ? 0x70000200u : payload;
    const uint32_t dmaAddress = scratch ? 0x80000200u : payload;
    finish(m, cpuAddress);
    // A stale TADR contains a valid zero-payload terminal. Reading it would
    // silently drop the supplied FINISH, just as the retail caller's stale
    // texture chain previously replaced its scratchpad menu commands.
    tag(m, head, 7, 0);
    const bool ends = id == 0 || id == 7 || (irq && tie);
    const uint32_t next = id == 1 ? dmaAddress + 32 : head;
    if (!ends || empty) {
        const uint32_t nextCpu = scratch && id == 1 ? cpuAddress + 32 : next;
        tag(m, nextCpu, 7, 2);
        finish(m, nextCpu + 16);
    }
    m.write32(gif + 0x10, dmaAddress);
    m.write32(gif + 0x20, empty ? 0 : 2);
    m.write32(gif + 0x30, head);
    const uint32_t chcr = 0x105u | (id << 28) | (irq ? 0x80000000u : 0) | (tie ? 0x80u : 0);
    m.write32(gif, chcr);
    check(delivered.size() == (empty || ends ? 32u : 64u), "exact payload size and continuation");
    check((m.read64(0x12001000u) & 2u) != 0, "preloaded FINISH reaches real GS CSR");
    check(m.read32(gif + 0x30) == (!empty && id == 1 ? next : head), "TADR follows active tag semantics");
    if (!empty) check(m.read32(gif + 0x10) == dmaAddress + 32, "MADR advances through preloaded payload");
    check(m.read32(gif + 0x20) == 0, "QWC retires");
    check((m.read32(gif) & 0x100u) == 0, "STR clears");
    check((m.read32(gif) >> 16) == (!empty && ends ? chcr >> 16 : 0x7000u), "active or final CHCR.TAG retained");
    check(m.consumeCompletedDmacCauses() == std::vector<uint32_t>{2}, "one completion cause");
}
void fastPathGuards() {
    PS2Memory m;
    check(m.initialize(), "fast-path memory initialized");
    GS gs;
    gs.init(m.getGSVRAM(), PS2_GS_VRAM_SIZE, &m.gs());
    tag(m, head, 7, 2);
    finish(m, head + 16);
    m.write32(gif + 0x20, 2);
    check(!m.tryProcessNativeGifPackedChain(gs, head, 0x105), "PACKED fast path rejects active QWC");
    check(m.read32(gif + 0x20) == 2, "rejected fast path does not retire payload");
    check(m.consumeCompletedDmacCauses().empty(), "rejected fast path does not complete DMA");
    m.write32(gif + 0x20, 0);
    check(m.tryProcessNativeGifPackedChain(gs, head, 0x105), "PACKED fast path still accepts a fresh chain");
    m.consumeCompletedDmacCauses();

    tag(m, head, 1, 5);
    m.write64(head + 16, (1ull << 60) | 4u);
    m.write64(head + 24, 0x0e);
    const uint64_t values[] = {1ull << 48, 0, (1ull << 32) | 4u, 0};
    for (uint32_t i = 0; i < 4; ++i) {
        m.write64(head + 32 + i * 16, values[i]);
        m.write64(head + 40 + i * 16, 0x50 + i);
    }
    tag(m, head + 96, 1, 1);
    m.write64(head + 112, (2ull << 58) | 0x8001u); // IMAGE, one quadword.
    m.write64(head + 120, 0);
    tag(m, head + 128, 7, 1);
    m.write64(head + 144, 0x8877665544332211ull);
    m.write64(head + 152, 0x1122334455667788ull);
    m.write32(gif + 0x20, 2);
    check(!m.tryProcessNativeGifImageUploadChain(gs, head, 0x105), "IMAGE fast path rejects active QWC");
    check(m.read32(gif + 0x20) == 2, "rejected IMAGE does not retire payload");
    check(m.consumeCompletedDmacCauses().empty(), "rejected IMAGE does not complete DMA");
    m.write32(gif + 0x20, 0);
    check(m.tryProcessNativeGifImageUploadChain(gs, head, 0x105), "IMAGE fast path still accepts a fresh chain");
}
void vifPayload(uint32_t channel, bool tte) {
    PS2Memory m;
    check(m.initialize(), "VIF memory initialized");
    tag(m, head, 7, 0);
    m.write32(0x70000100u, 0x07005aa5u); // MARK, then three NOPs.
    m.write32(0x70000104u, 0);
    m.write64(0x70000108u, 0);
    m.write32(channel + 0x10, 0x80000100u);
    m.write32(channel + 0x20, 1);
    m.write32(channel + 0x30, head);
    m.write32(channel, tte ? 0x145 : 0x105);
    const auto mark = channel == 0x10008000u ? m.vif0_regs.mark : m.vif1_regs.mark;
    check(mark == 0x5aa5, "VIF consumes preloaded scratchpad MARK without fetching tag bytes");
    check((m.read32(channel) & 0x100) == 0 && m.read32(channel + 0x20) == 0, "VIF retires payload");
    check(m.read32(channel + 0x30) == head, "VIF terminal preloaded tag retains TADR");
}
}
int main() {
    for (uint32_t id : {0u, 7u, 1u, 2u, 3u, 4u, 5u, 6u}) {
        run(id, false, false, false);
        run(id, false, true, true);
        run(id, false, true, false);
    }
    run(0, true, false, false);
    run(7, true, false, false);
    run(0, false, false, false, true);
    fastPathGuards();
    for (uint32_t channel : {0x10008000u, 0x10009000u}) {
        vifPayload(channel, false);
        vifPayload(channel, true);
    }
    std::cout << checks << " preloaded DMA checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
