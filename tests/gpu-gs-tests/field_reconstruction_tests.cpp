// SPDX-License-Identifier: GPL-3.0-only
#include "gs_field_reconstruction.h"
#include <cstdio>
#include <stdexcept>

namespace {
constexpr unsigned width = 8, height = 8, stride = 10;
std::vector<uint8_t> field(unsigned phase)
{
    std::vector<uint8_t> pixels(stride * height * 4, 0);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const auto i = (y * stride + x) * 4;
            pixels[i] = uint8_t((y / 2 * 2 + phase) * 20);
            pixels[i + 1] = uint8_t(x * 20);
            pixels[i + 2] = 90; pixels[i + 3] = 255;
        }
    return pixels;
}
void expect(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void full(const std::vector<uint8_t> &p)
{
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            expect(p[(y * stride + x) * 4] == y * 20, "stationary complementary rows retain full detail");
}
}
int main()
{
    try {
        GSFieldReconstruction reconstruction;
        auto apply = [&](unsigned phase, uint64_t generation, unsigned base = 0) {
            auto p = field(phase);
            reconstruction.reconstruct(p, width, height, stride, phase, generation, base);
            return p;
        };
        expect(apply(0, 1) == field(0), "first field must not borrow uninitialized history");
        expect(apply(1, 19) == field(1), "a new phase needs a stationary comparison");
        full(apply(0, 20)); full(apply(1, 40));
        full(apply(1, 40)); // repeated presentation, not a new field
        full(apply(0, 400)); // skipped fields and arbitrary presentation rate

        auto changed = field(0);
        for (unsigned y = 2; y < 4; ++y) changed[(y * stride + 3) * 4] = 200;
        const auto source = changed;
        reconstruction.reconstruct(changed, width, height, stride, 0, 401, 0);
        expect(changed[(3 * stride + 3) * 4] == 140, "moving even fields interpolate the rows above and below");
        auto repeat = source;
        reconstruction.reconstruct(repeat, width, height, stride, 0, 401, 0);
        expect(repeat == changed, "duplicate presentation must not turn motion into stationary detail");
        repeat = source;
        reconstruction.reconstruct(repeat, width, height, stride, 0, 450, 0);
        expect(repeat[(3 * stride + 3) * 4] == 140, "stale opposite pixels stay invalid until that field is refreshed");
        expect(apply(0, 451, 16) == field(0), "changed viewport offset resets history");
        reconstruction.reset();
        expect(apply(1, 1) == field(1), "device reset clears old fields");

        // Fading high-frequency detail: the missing lines must come from the
        // complementary field, even though brightness is changing every time.
        auto faded = [](unsigned phase, unsigned brightness) {
            auto p = field(phase);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                    for (unsigned c = 0; c < 3; ++c)
                        p[(y * stride + x) * 4 + c] = uint8_t(brightness + (y / 2 * 2 + phase) * 2 + x);
            return p;
        };
        reconstruction.reset();
        std::vector<uint8_t> prior[2];
        unsigned present = 0;
        for (unsigned step = 0; step < 80; ++step) {
            const unsigned phase = step & 1;
            const unsigned brightness = 30 + (step < 40 ? step : 80 - step) * 2;
            auto p = faded(phase, brightness);
            const auto raw = p;
            reconstruction.reconstruct(p, width, height, stride, phase, ++present, 0);
            if (step >= 2) {
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width; ++x)
                        for (unsigned c = 0; c < 3; ++c) {
                            const auto at = (y * stride + x) * 4 + c;
                            const auto &expected = (y & 1) == phase ? raw : prior[phase ^ 1];
                            expect(p[at] == expected[at], "fading detail must keep complementary scanlines in both directions");
                        }
            }
            prior[phase] = raw;
            auto duplicate = raw;
            reconstruction.reconstruct(duplicate, width, height, stride, phase, present, 0);
            expect(p == duplicate, "extra host presentations must not advance the fade history");
        }

        // Slow repeated-phase changes cannot indefinitely borrow a stale field.
        for (unsigned step = 1; step <= 12; ++step) {
            auto p = faded(1, 32 + step * 4);
            reconstruction.reconstruct(p, width, height, stride, 1, ++present, 0);
            if (step >= 8)
                expect(p[(2 * stride + 3) * 4] == 32 + step * 4 + 4 + 3,
                       "accumulated changes reject stale rows and interpolate odd fields in the correct direction");
        }

        // A cut must use only the new field, preserving captured rows, alpha,
        // and padding. This also exercises both clamped image boundaries.
        for (unsigned phase = 0; phase < 2; ++phase) {
            auto p = field(phase);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                    for (unsigned c = 0; c < 3; ++c) p[(y * stride + x) * 4 + c] = 240;
            const auto raw = p;
            reconstruction.reconstruct(p, width, height, stride, phase, ++present, 0);
            expect(p == raw, "scene cuts must not retain old glyphs or touch padding");
        }
        std::puts("field reconstruction: detail, full fade cycles, motion, cuts, repeated/skipped fields, and resets passed");
    } catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
