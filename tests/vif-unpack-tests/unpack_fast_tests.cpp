// SPDX-License-Identifier: GPL-3.0-or-later
// Source-authored real-parser fixture. Rasterization is substituted by the
// existing cooperative-dma stubs; no retail data, VU execution or game timing.
#include "runtime/ps2_memory.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

unsigned vifBulkUnpackCalls = 0;
static unsigned checks = 0;
static void require(bool v, const char *message)
{
    ++checks;
    if (!v) throw std::runtime_error(message);
}
static uint32_t command(unsigned op, unsigned num, unsigned imm)
{
    return (op << 24u) | (num << 16u) | imm;
}
static void word(std::vector<uint8_t> &bytes, uint32_t value)
{
    const auto old = bytes.size();
    bytes.resize(old + 4);
    std::memcpy(bytes.data() + old, &value, 4);
}
static void reset(PS2Memory &memory)
{
    memory.cancelVif1Work();
    std::memset(&memory.vif1_regs, 0, sizeof(memory.vif1_regs));
    std::memset(memory.getVU1Data(), 0xA5, PS2_VU1_DATA_SIZE);
    memory.vif1_regs.tops = 0x3F9;
    for (unsigned f = 0; f != 4; ++f)
    {
        memory.vif1_regs.row[f] = 0x10FF0050u + f;
        memory.vif1_regs.col[f] = 0x80903000u + f;
    }
}

#include "unpack_v4_16_cases.inc"

int main()
{
    try
    {
        PS2Memory memory;
        require(memory.initialize(), "real memory allocation");
        const std::array<unsigned, 10> sizes{1, 2, 3, 4, 7, 16, 31, 127, 255, 256};
        const std::array<unsigned, 8> cycles{0, 0x0101, 0x0103, 0x0204, 0x01FF, 0xFFFF, 0x0203, 0x0308};
        const std::array<unsigned, 6> starts{0, 1, 511, 1000, 1020, 1023};
        unsigned expectedFastCalls = 0;
        testV4_16(memory, expectedFastCalls);
        for (const unsigned count : sizes)
        for (const unsigned cycle : cycles)
        for (const unsigned start : starts)
        for (unsigned flags = 0; flags != 4; ++flags)
        for (unsigned mode = 0; mode != 4; ++mode)
        {
            reset(memory);
            memory.vif1_regs.cycle = cycle;
            memory.vif1_regs.mode = mode;
            const unsigned cl = std::max(1u, cycle & 255u), wl = std::max(1u, cycle >> 8u);
            const auto immediate = start | (flags << 14u);
            const auto code = command(0xEC, count & 255u, immediate); // V4-32 + IRQ
            std::vector<uint8_t> packet;
            word(packet, code);
            for (unsigned n = 0; n != count * 4; ++n)
                word(packet, 0x7FFF1234u * (n + 1u) + (flags << 30u));
            std::array<uint8_t, PS2_VU1_DATA_SIZE> expected;
            std::memcpy(expected.data(), memory.getVU1Data(), expected.size());
            auto registers = memory.vif1_regs;
            registers.code = code;
            registers.num = count & 255u;
            registers.stat |= 1u << 11u;
            const unsigned first = (start + ((flags & 2u) ? registers.tops : 0u)) % 1024u;
            for (unsigned n = 0; n != count; ++n)
            {
                const unsigned destination = (first + (n / wl) * cl + n % wl) % 1024u;
                for (unsigned f = 0; f != 4; ++f)
                {
                    uint32_t value;
                    std::memcpy(&value, packet.data() + 4u + 16u * n + 4u * f, 4);
                    if (mode == 1 || mode == 2) value += registers.row[f];
                    if (mode == 2 || mode == 3) registers.row[f] = value;
                    std::memcpy(expected.data() + destination * 16u + f * 4u, &value, 4);
                }
            }
            const auto priorFast = vifBulkUnpackCalls;
            memory.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));
            require(std::memcmp(memory.getVU1Data(), expected.data(), expected.size()) == 0, "V4-32 destination/full memory exact");
            require(std::memcmp(&memory.vif1_regs, &registers, sizeof(registers)) == 0, "VIF registers and ROW exact");
#if EXPECT_BULK_UNPACK
            const unsigned admitted = mode == 0 ? 1u : 0u;
            expectedFastCalls += admitted;
            require(vifBulkUnpackCalls - priorFast == admitted, "fast-path admission or mode fallback observed");
#else
            require(vifBulkUnpackCalls == 0, "baseline has no injected fast-path witness");
#endif
        }

        // Mask, ROW-changing mode and fill-write cases retain general decoding.
        for (const unsigned cycle : {0x0101u, 0x0301u, 0x0703u})
        for (const uint32_t mask : {0u, 0xE4E4E4E4u, 0xFFFFFFFFu, 0x55555555u})
        for (unsigned mode = 0; mode != 4; ++mode)
        {
            reset(memory);
            memory.vif1_regs.cycle = cycle;
            memory.vif1_regs.mask = mask;
            memory.vif1_regs.mode = mode;
            const unsigned count = 9, cl = cycle & 255u, wl = cycle >> 8u;
            const unsigned sourceCount = cl < wl ? count / wl * cl + std::min(count % wl, cl) : count;
            const auto code = command(0x7C, count, 1020);
            std::vector<uint8_t> input;
            word(input, code);
            for (unsigned n = 0; n != sourceCount * 4; ++n) word(input, 0x10000000u + n);
            std::array<uint8_t, PS2_VU1_DATA_SIZE> expected;
            std::memcpy(expected.data(), memory.getVU1Data(), expected.size());
            auto registers = memory.vif1_regs;
            registers.code = code; registers.num = count;
            unsigned source = 0;
            for (unsigned n = 0; n != count; ++n)
            {
                const unsigned slot = n % wl;
                const bool consumes = cl >= wl || slot < cl;
                const unsigned destination = (1020u + (cl >= wl ? n / wl * cl + slot : n)) % 1024u;
                for (unsigned f = 0; f != 4; ++f)
                {
                    unsigned selector = (mask >> (std::min(slot, 3u) * 8u + f * 2u)) & 3u;
                    if (!consumes && selector == 0) selector = 1;
                    if (selector == 3) continue;
                    uint32_t value;
                    if (selector == 1) value = registers.row[f];
                    else if (selector == 2) value = registers.col[std::min(slot, 3u)];
                    else
                    {
                        value = 0x10000000u + source * 4u + f;
                        if (mode == 1 || mode == 2) value += registers.row[f];
                        if (mode == 2 || mode == 3) registers.row[f] = value;
                    }
                    std::memcpy(expected.data() + destination * 16u + f * 4u, &value, 4);
                }
                if (consumes) ++source;
            }
            const auto priorFast = vifBulkUnpackCalls;
            memory.processVIF1Data(input.data(), static_cast<uint32_t>(input.size()));
            require(std::memcmp(memory.getVU1Data(), expected.data(), expected.size()) == 0, "masked/fill fallback exact");
            require(std::memcmp(&memory.vif1_regs, &registers, sizeof(registers)) == 0, "fallback ROW/registers exact");
            require(vifBulkUnpackCalls == priorFast, "masked/fill command not admitted");
        }

        // Even unusual direct API callers that alias VU memory keep the original
        // sequential vector read/write behavior; bulk memcpy must not run.
        reset(memory);
        std::vector<uint8_t> aliased;
        word(aliased, command(0x6C, 3, 11));
        for (unsigned n = 0; n != 12; ++n) word(aliased, 0xCC000000u + n);
        std::memcpy(memory.getVU1Data() + 160u, aliased.data(), aliased.size());
        std::array<uint8_t, PS2_VU1_DATA_SIZE> aliasExpected;
        std::memcpy(aliasExpected.data(), memory.getVU1Data(), aliasExpected.size());
        for (unsigned n = 0; n != 3; ++n)
        {
            std::array<uint8_t, 16> vector;
            std::memcpy(vector.data(), aliasExpected.data() + 164u + n * 16u, 16);
            std::memcpy(aliasExpected.data() + (11u + n) * 16u, vector.data(), 16);
        }
        const auto beforeAlias = vifBulkUnpackCalls;
        memory.processVIF1Data(memory.getVU1Data() + 160u, static_cast<uint32_t>(aliased.size()));
        require(std::memcmp(memory.getVU1Data(), aliasExpected.data(), aliasExpected.size()) == 0, "aliased sequential vector behavior exact");
        require(vifBulkUnpackCalls == beforeAlias, "overlapping input uses general path");

        // Parser ownership and partial-input behavior must survive the shortcut.
        std::vector<uint8_t> packet;
        word(packet, command(0x6C, 4, 1023));
        for (unsigned n = 0; n != 16; ++n) word(packet, 0xF0001000u + n);
        memory.setVu1CooperativeCallbacks([] { return false; }, [] {});
        for (size_t split = 0; split <= packet.size(); ++split)
        {
            reset(memory);
            memory.processVIF1Data(packet.data(), static_cast<uint32_t>(split));
            memory.serviceVif1Stream();
            if (split != packet.size())
            {
                for (unsigned n = 0; n != PS2_VU1_DATA_SIZE; ++n)
                    require(memory.getVU1Data()[n] == 0xA5, "partial command cannot mutate destination");
            }
            memory.processVIF1Data(packet.data() + split, static_cast<uint32_t>(packet.size() - split));
            memory.serviceVif1Stream();
            require(!memory.vif1WorkPending(), "split command fully consumed");
            for (unsigned n = 0; n != 16; ++n)
            {
                uint32_t value;
                std::memcpy(&value, memory.getVU1Data() + ((1023u * 4u + n) % 4096u) * 4u, 4);
                require(value == 0xF0001000u + n, "split payload and VU wrap exact");
            }
#if EXPECT_BULK_UNPACK
            ++expectedFastCalls;
#endif
        }
        require(vifBulkUnpackCalls == expectedFastCalls, "all admitted calls counted");
        std::cout << "vif-unpack checks=" << checks << " fast-calls=" << vifBulkUnpackCalls << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << " checks=" << checks << '\n';
        return 1;
    }
}
