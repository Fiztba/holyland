// The renderer core of native draw (port/include/port/render.h): meshes parsed from PS2 data blocks and
// cached, and the per-frame side buffer of draws.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "render_internal.hpp"

RdStats rd_stats;

namespace {

// ---- Parsing a block ----------------------------------------------------------------------------
// A block is a stream of VIF codes, as VIF1 reads it (port/platform/vif1.c is the model): UNPACKs into
// VU1 memory relative to TOPS (FLG), and MSCAL/MSCNT, each a VU1 run of the batch at TOP. VU1 memory
// is modelled as far as the batches need it: two buffers for TOPS to alternate between, and which of
// their qwords this block has written. A batch whose tag, vertices or A+D packet the block did not
// write itself (left by the packets before it on the PS2) is not something the block alone describes:
// the block is refused.
const uint32_t VU_QWORDS = 1024;
const uint32_t BUFFER_A = 0x080, BUFFER_B = 0x240; // (the game's own TOPs: Packet_LayOutBuffers' BASE/OFFSET)
const uint32_t N_MAX = 39;                         // vu1_hle.c: more and the vertices reach TOP+0xA0

const char *why_refused = "";

struct VuModel {
    uint32_t mem[VU_QWORDS][4];
    uint8_t written[VU_QWORDS];
};

uint32_t unpack_bits(uint32_t cmd) {
    uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    return vl == 3 ? 16 : (vn + 1) * (32u >> vl);
}

uint32_t element(const uint8_t *bytes, uint32_t index, uint32_t vl, bool usn) {
    switch (vl) {
    case 0: {
        uint32_t v;
        memcpy(&v, bytes + index * 4, 4);
        return v;
    }
    case 1: {
        uint16_t v;
        memcpy(&v, bytes + index * 2, 2);
        return usn ? v : uint32_t(int32_t(int16_t(v)));
    }
    default: {
        uint8_t v = bytes[index];
        return usn ? v : uint32_t(int32_t(int8_t(v)));
    }
    }
}

// The batch at top (a VU1 run of program 0x000's shape: vu1_hle.c), into mesh
bool take_batch(const VuModel &vu, uint32_t top, uint32_t start, RdMeshData &mesh) {
    if (!vu.written[top]) {
        why_refused = "a batch's GIF tag is not the block's";
        return false;
    }
    RdBatch b;
    memcpy(b.tag, vu.mem[top], 16);
    uint32_t n = uint32_t(b.tag[0] & 0x7FFF);
    uint32_t nreg = uint32_t(b.tag[0] >> 60), flg = uint32_t((b.tag[0] >> 58) & 3);
    if (n < 3 || n > N_MAX) {
        why_refused = "a batch of fewer than 3 or more than 39 vertices";
        return false;
    }
    if (flg != 0 || nreg != 3 || (b.tag[1] & 0xFFF) != 0x412) {
        why_refused = "a batch whose GIF tag is not ST, RGBAQ, XYZF2";
        return false;
    }
    if (uint16_t(b.tag[1] >> 32) != 0) {
        why_refused = "a batch tag with w set (vu1_hle.c's ADC for every vertex)";
        return false;
    }
    for (uint32_t q = top + 1; q < top + 1 + 4 * n; q++) {
        if (!vu.written[q % VU_QWORDS]) {
            why_refused = "a batch's vertices are not all the block's";
            return false;
        }
    }
    // the A+D packet at TOP+0xA0: a GIF tag (PACKED, A+D) and its writes
    uint32_t ad = top + 0xA0;
    if (!vu.written[ad % VU_QWORDS]) {
        why_refused = "a batch's A+D packet is not the block's";
        return false;
    }
    uint64_t adtag[2];
    memcpy(adtag, vu.mem[ad % VU_QWORDS], 16);
    uint32_t ad_n = uint32_t(adtag[0] & 0x7FFF), ad_nreg = uint32_t(adtag[0] >> 60);
    if (((adtag[0] >> 58) & 3) != 0 || ad_nreg != 1 || (adtag[1] & 0xF) != 0xE || ad_n > 16) {
        why_refused = "a batch's A+D packet is not A+D";
        return false;
    }
    for (uint32_t i = 0; i < ad_n; i++) {
        uint32_t q = (ad + 1 + i) % VU_QWORDS;
        if (!vu.written[q]) {
            why_refused = "a batch's A+D writes are not all the block's";
            return false;
        }
        uint64_t w[2];
        memcpy(w, vu.mem[q], 16);
        b.ad.push_back(w[1] & 0xFF);
        b.ad.push_back(w[0]);
    }
    b.start = start;
    b.first = uint32_t(mesh.vertices.size() / 16);
    b.count = n;
    b.alpha_lo = 255;
    b.alpha_hi = 0;
    for (uint32_t k = 0; k < n; k++) {
        for (uint32_t q = 0; q < 4; q++) {
            const uint32_t *src = vu.mem[(top + 1 + 4 * k + q) % VU_QWORDS];
            for (int f = 0; f < 4; f++) {
                mesh.vertices.push_back(int32_t(src[f]));
            }
        }
        // (the colour's alpha as VU1 leaves it: min(1 * a, 255), FTOI0)
        int32_t a = int32_t(vu.mem[(top + 4 + 4 * k) % VU_QWORDS][3]);
        uint32_t ac = a < 0 ? 0u : a > 255 ? 255u : uint32_t(a);
        b.alpha_lo = std::min(b.alpha_lo, ac);
        b.alpha_hi = std::max(b.alpha_hi, ac);
    }
    // The triangles, as the GS queues the kicks (gsn.cpp, vertex_kick): a kick whose XYZF2 has ADC set
    // by the data (the normal's w, ORed into it by the program) draws nothing. Clipping's ADC is the
    // frame's (gsn_native_draw.inc).
    uint32_t prim = uint32_t((b.tag[0] >> 47) & 0x7FF);
    if (!((b.tag[0] >> 46) & 1)) {
        why_refused = "a batch tag without PRE";
        return false;
    }
    uint32_t type = prim & 7;
    for (uint32_t k = 0; k < n; k++) {
        uint16_t nw = uint16_t(vu.mem[(top + 2 + 4 * k) % VU_QWORDS][3]);
        bool adc = (nw & 0x8000) != 0;
        int a = -1, bb = -1;
        if (type == 3 && k % 3 == 2) {
            a = int(k) - 2;
            bb = int(k) - 1;
        } else if (type == 4 && k >= 2) {
            a = int(k) - 2;
            bb = int(k) - 1;
        } else if (type == 5 && k >= 2) {
            a = 0;
            bb = int(k) - 1;
        } else if (type != 3 && type != 4 && type != 5) {
            why_refused = "a batch of points, lines or sprites";
            return false;
        }
        if (a >= 0 && !adc) {
            b.triangles.push_back(uint8_t(a));
            b.triangles.push_back(uint8_t(bb));
            b.triangles.push_back(uint8_t(k));
        }
    }
    mesh.batches.push_back(std::move(b));
    return true;
}

bool parse_block(const uint8_t *bytes, uint32_t qwords, RdMeshData &mesh) {
    static VuModel vu; // (the game's thread only)
    memset(vu.written, 0, sizeof(vu.written));
    uint32_t cl = 1, wl = 1, mode = 0, tops = BUFFER_A;
    bool dbf = false;
    uint32_t row[4] = {};
    size_t words = size_t(qwords) * 4, i = 0;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(bytes);
    while (i < words) {
        uint32_t code = w[i++];
        uint32_t cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
        if (cmd >= 0x60) {
            uint32_t n = num ? num : 256;
            uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
            bool masked = (cmd & 0x10) != 0, usn = (imm >> 14) & 1;
            if (masked) {
                why_refused = "a masked UNPACK";
                return false;
            }
            if (wl == 0 || cl < wl) {
                why_refused = "an UNPACK with filling (CL < WL)";
                return false;
            }
            uint32_t need = (n * unpack_bits(cmd) + 31) / 32;
            if (i + need > words) {
                why_refused = "an UNPACK past the block's end";
                return false;
            }
            if (!((imm >> 15) & 1)) {
                why_refused = "an UNPACK to a fixed address (not relative to TOPS)";
                return false;
            }
            uint32_t address = (imm & 0x3FF) + tops;
            const uint8_t *data = reinterpret_cast<const uint8_t *>(w + i);
            for (uint32_t k = 0; k < n; k++) {
                uint32_t q = (address + (k / wl) * cl + k % wl) % VU_QWORDS;
                uint32_t v[4];
                if (vl == 3) {
                    uint16_t h;
                    memcpy(&h, data + k * 2, 2);
                    v[0] = uint32_t(h & 0x1F) << 3;
                    v[1] = uint32_t((h >> 5) & 0x1F) << 3;
                    v[2] = uint32_t((h >> 10) & 0x1F) << 3;
                    v[3] = uint32_t((h >> 15) & 1) << 7;
                } else if (vn == 0) {
                    v[0] = v[1] = v[2] = v[3] = element(data, k, vl, usn);
                } else if (vn == 1) {
                    v[0] = v[2] = element(data, k * 2, vl, usn);
                    v[1] = v[3] = element(data, k * 2 + 1, vl, usn);
                } else if (vn == 2) {
                    v[0] = element(data, k * 3, vl, usn);
                    v[1] = element(data, k * 3 + 1, vl, usn);
                    v[2] = element(data, k * 3 + 2, vl, usn);
                    v[3] = (k + 1) * 3 < need * 32 / (32u >> vl) ? element(data, k * 3 + 3, vl, usn) : 0;
                } else {
                    for (uint32_t f = 0; f < 4; f++) {
                        v[f] = element(data, k * 4 + f, vl, usn);
                    }
                }
                for (int f = 0; f < 4; f++) {
                    if (mode == 1) {
                        v[f] += row[f];
                    } else if (mode == 2) {
                        row[f] += v[f];
                        v[f] = row[f];
                    }
                    vu.mem[q][f] = v[f];
                }
                vu.written[q] = 1;
            }
            i += need;
            continue;
        }
        switch (cmd) {
        case 0x00: // NOP
        case 0x06: // MSKPATH3
        case 0x07: // MARK
        case 0x10: // FLUSHE
        case 0x11: // FLUSH
        case 0x13: // FLUSHA
            break;
        case 0x01: // STCYCL
            cl = imm & 0xFF;
            wl = (imm >> 8) & 0xFF;
            break;
        case 0x05: // STMOD
            mode = imm & 3;
            break;
        case 0x02: // OFFSET, BASE, ITOP: the block's own double buffering is not modelled
        case 0x03:
        case 0x04:
            why_refused = "OFFSET, BASE or ITOP in a block";
            return false;
        case 0x14: // MSCAL
        case 0x15: // MSCALF
        case 0x17: // MSCNT
            if (!take_batch(vu, tops, cmd == 0x17 ? RD_PROGRAM_CONTINUE : imm, mesh)) {
                return false;
            }
            // (VIF1 flips TOPS at each run; what the block writes next goes to the other buffer)
            dbf = !dbf;
            tops = dbf ? BUFFER_B : BUFFER_A;
            memset(vu.written, 0, sizeof(vu.written));
            break;
        case 0x20: // STMASK
            i += 1;
            break;
        case 0x30: // STROW
            if (i + 4 > words) {
                return false;
            }
            memcpy(row, w + i, 16);
            i += 4;
            break;
        case 0x31: // STCOL
            i += 4;
            break;
        default:
            why_refused = "a VIF code a block should not have (MPG, DIRECT ...)";
            return false;
        }
    }
    if (mesh.batches.empty()) {
        why_refused = "no MSCAL or MSCNT";
        return false;
    }
    return true;
}

uint64_t hash_bytes(const uint8_t *p, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    for (; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

// ---- The mesh cache (the game's thread) ---------------------------------------------------------
struct CacheEntry {
    uint32_t gen;      // the caller's, or the bytes' hash folded (RD_GEN_HASH)
    uint32_t touched;  // rd_mesh_touch since
    uint32_t frame;    // last asked for
    std::shared_ptr<const RdMeshData> mesh; // (null: refused)
};
std::unordered_map<uint32_t, CacheEntry> cache;
std::unordered_map<RdMesh, std::shared_ptr<const RdMeshData>> by_id; // (the meshes cache holds)
RdMesh next_id = 1;

// ---- KF4_ND_DUMP=FILE (debug only; NATIVE_DRAW.md's open questions, P0-3) ------------------------
// A text file of what the core is handed, for tools/nd_measure.py: per frame "F serial parsed reparsed
// hashed_bytes hash_ns parse_ns"; per new mesh "M id key batches", then per batch "B start prim alpha_lo alpha_hi
// count" + "A reg value ..." + "V x y z w a ..." (positions, ITOF0 as VU1 reads them, and the colour's alpha) + "T i j k ...";
// per draw "D kind id0 id1 id2 m[16] meshes...", and the adapter's notes (rd_dump_note). Off: nothing.
struct Dump {
    FILE *f = nullptr;
    bool checked = false;
    uint64_t parsed = 0, reparsed = 0, hashed = 0, hash_ns = 0, parse_ns = 0;
    std::unordered_map<RdMesh, bool> written;
    FILE *file() {
        if (!checked) {
            checked = true;
            const char *path = getenv("KF4_ND_DUMP");
            if (path && *path) {
                f = fopen(path, "w");
            }
        }
        return f;
    }
} dump;

void dump_mesh(const RdMeshData &m) {
    FILE *f = dump.f;
    fprintf(f, "M %u %#x %zu\n", m.id, m.key, m.batches.size());
    for (const RdBatch &b : m.batches) {
        fprintf(f, "B %#x %#llx %u %u %u\n", b.start, (unsigned long long)((b.tag[0] >> 47) & 0x7FF), b.alpha_lo,
                b.alpha_hi, b.count);
        fprintf(f, "A");
        for (uint64_t v : b.ad) {
            fprintf(f, " %#llx", (unsigned long long)v);
        }
        fprintf(f, "\nV");
        for (uint32_t k = 0; k < b.count; k++) {
            const int32_t *v = &m.vertices[size_t(b.first + k) * 16];
            fprintf(f, " %d %d %d %d %d", v[0], v[1], v[2], v[3], v[15]);
        }
        fprintf(f, "\nT");
        for (uint8_t t : b.triangles) {
            fprintf(f, " %u", t);
        }
        fprintf(f, "\n");
    }
}

// ---- The side buffer ----------------------------------------------------------------------------
const uint32_t SLOTS = 128; // (a power of two: the 24-bit serial wraps onto the same slot)
struct Slot {
    uint32_t serial = 0;
    std::vector<RdDraw> draws;
    std::vector<uint32_t> first_mesh; // each draw's first in meshes
    std::vector<std::shared_ptr<const RdMeshData>> meshes;
    std::vector<RdCamera> cameras;
};
std::mutex slots_lock; // (the game's thread writes the slot of its frame; the GS's reads older ones)
Slot slots[SLOTS];
uint32_t serial = 0; // the frame's (24 bits in a handle)
uint32_t frame_count = 0;
// the consumer (the native GS): the last frame whose RD_ND_FRAME mark it has met (rd_consumer_frame)
std::atomic<uint32_t> consumer_frame{0};
std::atomic<bool> consumer{false};

} // namespace

extern "C" RdMesh rd_mesh_from_block(const void *block, uint32_t qwords, uint32_t key, uint32_t gen, RdVertexFormat fmt) {
    const uint8_t *bytes = static_cast<const uint8_t *>(block);
    uint32_t want = gen;
    if (gen == RD_GEN_HASH) {
        auto t0 = dump.file() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
        uint64_t h = hash_bytes(bytes, size_t(qwords) * 16);
        want = uint32_t(h ^ (h >> 32)) | 1u;
        rd_stats.hash_bytes += uint64_t(qwords) * 16;
        if (dump.f) { // (KF4_ND_DUMP)
            dump.hash_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            dump.hashed += uint64_t(qwords) * 16;
        }
    }
    auto it = cache.find(key);
    if (dump.f && !(it != cache.end() && it->second.gen == want && !it->second.touched)) { // (KF4_ND_DUMP)
        dump.parsed++;
        if (it != cache.end()) {
            dump.reparsed++;
            fprintf(dump.f, "P %#x %u\n", key, qwords);
        }
    }
    if (it != cache.end() && it->second.gen == want && !it->second.touched) {
        it->second.frame = frame_count;
        rd_stats.mesh_hits++;
        if (!it->second.mesh) {
            why_refused = "refused before";
            return 0;
        }
        return it->second.mesh->id;
    }
    auto mesh = std::make_shared<RdMeshData>();
    mesh->key = key;
    mesh->gen = want;
    rd_stats.meshes_parsed++;
    auto p0 = dump.f ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    bool ok = fmt == RD_VF_PACKED16 && parse_block(bytes, qwords, *mesh);
    if (dump.f) { // (KF4_ND_DUMP)
        dump.parse_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - p0).count());
    }
    if (fmt != RD_VF_PACKED16) {
        why_refused = "only RD_VF_PACKED16 is parsed so far";
    }
    CacheEntry &e = cache[key];
    if (e.mesh) {
        by_id.erase(e.mesh->id);
    }
    e.gen = want;
    e.touched = 0;
    e.frame = frame_count;
    if (!ok) {
        e.mesh.reset();
        rd_stats.mesh_unsupported++;
        static const char *log = getenv("KF4_ND_LOG");
        if (log) {
            fprintf(stderr, "[nd] block %#x (%u qwords) refused: %s\n", key, qwords, why_refused);
        }
        return 0;
    }
    mesh->id = next_id++;
    if (next_id == 0) {
        next_id = 1;
    }
    e.mesh = mesh;
    by_id[mesh->id] = mesh;
    rd_stats.meshes_held = by_id.size();
    return mesh->id;
}

extern "C" int rd_dump_enabled(void) {
    return dump.file() != nullptr;
}

extern "C" void rd_dump_note(const char *line) {
    if (dump.file()) {
        fprintf(dump.f, "%s\n", line);
    }
}

extern "C" void rd_mesh_touch(uint32_t key) {
    auto it = cache.find(key);
    if (it != cache.end()) {
        it->second.touched = 1;
    }
}

extern "C" const char *rd_mesh_why(void) {
    return why_refused;
}

extern "C" uint32_t rd_mesh_program(RdMesh mesh) {
    auto it = by_id.find(mesh);
    return it == by_id.end() || it->second->batches.empty() ? RD_PROGRAM_CONTINUE : it->second->batches[0].start;
}

extern "C" uint64_t rd_begin_frame(void) {
    // The slot this frame takes was a frame's SLOTS ago: if the consumer has not got that far (its
    // pictures between are drawn a frame after it), wait for it. (The GS's ring of 64 MB holds some 50
    // frames of play: this is a guard that should not be met. Not for ever: after 2 s the draws go.)
    uint32_t next = (serial + 1) & 0xFFFFFF;
    if (consumer.load() && !slots[next % SLOTS].draws.empty()) {
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (((consumer_frame.load() - slots[next % SLOTS].serial) & 0xFFFFFF) < 2 ||
               ((consumer_frame.load() - slots[next % SLOTS].serial) & 0x800000)) {
            if (std::chrono::steady_clock::now() > until) {
                rd_stats.waits_given_up++;
                break;
            }
            rd_stats.waits++;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    std::lock_guard<std::mutex> g(slots_lock);
    serial = next;
    frame_count++;
    if (dump.file()) { // (KF4_ND_DUMP: the frame before's counts, then this frame's serial)
        fprintf(dump.f, "F %u %llu %llu %llu %llu %llu\n", serial, (unsigned long long)dump.parsed,
                (unsigned long long)dump.reparsed, (unsigned long long)dump.hashed, (unsigned long long)dump.hash_ns,
                (unsigned long long)dump.parse_ns);
        dump.parsed = dump.reparsed = dump.hashed = dump.hash_ns = dump.parse_ns = 0;
        fflush(dump.f);
    }
    Slot &s = slots[serial % SLOTS];
    s.serial = serial;
    s.draws.clear();
    s.first_mesh.clear();
    s.meshes.clear();
    s.cameras.clear();
    // meshes not asked for in a while leave the cache (a slot that still names one keeps it alive)
    if (frame_count % 64 == 0) {
        for (auto it = cache.begin(); it != cache.end();) {
            if (frame_count - it->second.frame > 600) {
                if (it->second.mesh) {
                    by_id.erase(it->second.mesh->id);
                }
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
        rd_stats.meshes_held = by_id.size();
    }
    return (uint64_t(RD_ND_FRAME) << 56) | serial;
}

extern "C" void rd_consumer_frame(uint64_t value) {
    consumer_frame.store(uint32_t(value) & 0xFFFFFF);
    consumer.store(true);
}

extern "C" uint32_t rd_camera(const RdCamera *c) {
    std::lock_guard<std::mutex> g(slots_lock);
    Slot &s = slots[serial % SLOTS];
    if (!s.cameras.empty() && memcmp(&s.cameras.back(), c, sizeof(*c)) == 0) {
        return uint32_t(s.cameras.size() - 1);
    }
    s.cameras.push_back(*c);
    return uint32_t(s.cameras.size() - 1);
}

extern "C" uint64_t rd_draw(const RdDraw *d, const RdMesh *meshes, uint32_t count) {
    std::lock_guard<std::mutex> g(slots_lock);
    Slot &s = slots[serial % SLOTS];
    if (d->camera >= s.cameras.size()) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (by_id.find(meshes[i]) == by_id.end()) {
            return 0;
        }
    }
    uint32_t index = uint32_t(s.draws.size());
    s.draws.push_back(*d);
    s.draws.back().mesh_count = count;
    s.first_mesh.push_back(uint32_t(s.meshes.size()));
    for (uint32_t i = 0; i < count; i++) {
        s.meshes.push_back(by_id[meshes[i]]);
    }
    rd_stats.draws++;
    if (dump.file()) { // (KF4_ND_DUMP)
        for (uint32_t i = 0; i < count; i++) {
            if (!dump.written[meshes[i]]) {
                dump.written[meshes[i]] = true;
                dump_mesh(*by_id[meshes[i]]);
            }
        }
        fprintf(dump.f, "D %u %#x %u %u", d->kind, d->id[0], d->id[1], d->id[2]);
        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 4; r++) {
                fprintf(dump.f, " %.9g", d->model[c][r]);
            }
        }
        for (uint32_t i = 0; i < count; i++) {
            fprintf(dump.f, " %u", meshes[i]);
        }
        fprintf(dump.f, "\n");
    }
    return (uint64_t(RD_ND_DRAW) << 56) | (uint64_t(serial) << 32) | index;
}

bool rd_fetch(uint64_t value, RdDraw &draw, RdCamera &camera, std::vector<std::shared_ptr<const RdMeshData>> &meshes) {
    uint32_t want = uint32_t(value >> 32) & 0xFFFFFF, index = uint32_t(value);
    std::lock_guard<std::mutex> g(slots_lock);
    const Slot &s = slots[want % SLOTS];
    if (s.serial != want || index >= s.draws.size()) {
        rd_stats.lost++;
        return false;
    }
    draw = s.draws[index];
    camera = s.cameras[draw.camera];
    meshes.assign(s.meshes.begin() + s.first_mesh[index], s.meshes.begin() + s.first_mesh[index] + draw.mesh_count);
    return true;
}
