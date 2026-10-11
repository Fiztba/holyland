// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_common.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

// Exercise the actual packet decoder and software rasterizer without optional
// recording/interpolation or a host window.
std::unique_ptr<GSRasterBackend> makeGsRasterBackend(GS *) {
    return std::make_unique<GSCpuBackend>();
}

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char *message, uint32_t z) {
    ++checks;
    if (!ok) { ++failures; std::cerr << message << " z=" << std::hex << z << std::dec << '\n'; }
}
void packed(GS &gs, uint8_t reg, uint32_t z, bool native, uint16_t x = 128, uint16_t y = 128) {
    std::array<uint64_t,4> packet{0x1000000000008001ull, reg,
        x | (uint64_t(y) << 32), z};
    if (native) check(gs.processNativePackedGIFPacket(reinterpret_cast<const uint8_t *>(packet.data()),32),
                      "native packet accepted",z);
    else gs.processGIFPacket(reinterpret_cast<const uint8_t *>(packet.data()),32);
}
void depth(uint32_t z, bool native) {
    std::vector<uint8_t> vram(4u << 20);
    GS gs; gs.init(vram.data(),static_cast<uint32_t>(vram.size()));
    gs.setDebugHistoryPaused(false);
    gs.writeRegister(GS_REG_FRAME_1,1ull << 16);
    gs.writeRegister(GS_REG_ZBUF_1,64u);
    gs.writeRegister(GS_REG_SCISSOR_1,(31ull << 16) | (31ull << 48));
    gs.writeRegister(GS_REG_PRMODECONT,1);
    gs.writeRegister(GS_REG_PRIM,GS_PRIM_POINT);
    gs.writeRegister(GS_REG_TEST_1,(1ull << 16) | (2ull << 17)); // Z >= previous
    gs.writeRegister(GS_REG_RGBAQ,0x804080c0u);
    gs.WriteVram(GS_PSM_Z32,64u * 32u,1,8,8,z);
    packed(gs,5,z,native);
    const auto events = gs.getDebugHistory();
    bool exact = false;
    for (const auto &e : events) if (e.kind == GSDebugEventKind::Draw)
        exact = e.zMin == double(z) && e.zMax == double(z);
    check(exact,"PACKED XYZ2 preserves all depth bits",z);
    check(gs.ReadVram(GS_PSM_CT32,0,1,8,8) == 0x804080c0u,"equal-depth point survives",z);
    check(gs.ReadVram(GS_PSM_Z32,64u * 32u,1,8,8) == z,"stored depth is exact",z);

    // XYZ3 supplies the first line vertex without drawing; XYZ2 kicks it.
    gs.clearDebugHistory();
    gs.writeRegister(GS_REG_PRIM,GS_PRIM_LINE);
    packed(gs,13,z,native,128,144);
    packed(gs,5,z,native,160,144);
    exact = false;
    for (const auto &e : gs.getDebugHistory()) if (e.kind == GSDebugEventKind::Draw)
        exact = e.zMin == double(z) && e.zMax == double(z);
    check(exact,"PACKED XYZ3 preserves all depth bits",z);
}
}
int main() {
    for (bool native : {false,true})
        for (uint32_t z : {0u,0x00ffffffu,0x01000001u,0x7fffffffu,0xffffff7fu,0xfffffffeu,0xffffffffu})
            depth(z,native);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
