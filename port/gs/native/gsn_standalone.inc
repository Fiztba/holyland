// ---- Standalone (P8): the scene drawn into its own targets -------------------------------------------
// (docs/port/NATIVE_DRAW.md 3.4 and P8; docs/port/NATIVE_DRAW_P8.md.) Included by gsn.cpp after
// gsn_native_draw.inc, inside its namespace: the native GS's own code, kept in a file of its own.
//
// With --renderer modern (gsn_set_standalone) the native draws of the scene's kinds (map records and the
// items lying about: sa_kind) are not drawn into the GS's targets with the GS's Z. They are drawn into the
// scene's own: a colour image as large as the frame target (the GS's colour format, so the GS's
// arithmetic, gs_frag.h, makes the same colours), multisampled as --native-msaa says, and a D32F depth
// whose numbers are the GS's Z before any rounding (larger nearer: reversed Z; shaders/nd_000.vert built
// as sa_000.vert). No rounding: a draw over a surface it lies on (the layers the game blends over its
// surfaces, signs, overlays) wins within a Z of it, Z first and then its colour (sa_draw_batch, "Ties"), as
// the GS's rounding made the later one win, without the GS's sawtooth and lace.
//
// Their textures come from where the game uploaded them, not from VRAM: the page-ownership table (below,
// "Who owns VRAM") records, in the chain's order, which upload (its bytes and where it went: the game RAM
// chunk the upload function sent) owns each 256-byte block, and which pages a render target or a copy
// wrote since. A texture whose blocks all belong to uploads is decoded once from those uploads alone,
// written into a scratch VRAM (gsn_vram.c's model) and read back as the GS samples it, and found again by
// the uploads it came from. A texture read from a render target (the scrolling textures' 128 x 128
// targets, the reflection, frame copies) or from a page a copy wrote is the GS's own (its targets and
// VRAM: for now; named targets are a P8 task).
//
// The scene and the GS's targets take turns ("islands"): the scene takes the frame target's picture and Z
// buffer when its first batch comes (sa_acquire: copies), and gives them back (sa_release: its colour
// copied, its depth as the GS keeps it) before anything else of the GS touches them: a batch the
// scene does not draw, an upload or copy, a vblank. So everything the scene does not draw yet (entities,
// effects, the arm, the passes, the 2D, the menus) is drawn by the GS over the scene's picture, in the
// chain's order: the 2D and the passes after the world are composited over it as the PS2 drew them. As
// more kinds move into the scene the turns get fewer, down to one a frame (the world, then the GS's 2D).
//
// What the scene cannot draw yet goes back to the GS whole (sa_decline, counted by reason): a batch that
// needs the GS's shader blending (DATE, a partial mask, PABE, ...), one into another target (the
// reflection's 256 x 256, the scrolling textures'), one that reads its own target.

const Target SA_MARKER = {}; // (gs->pass_color while the scene's render pass is under way)

volatile int sa_mode = 0; // (gsn_set_standalone)

enum {
    SA_WHY_FRAME,    // another target than a frame of 512 pixels or more across, 32 or 24-bit
    SA_WHY_TARGET,   // the frame and Z buffer could not be had together
    SA_WHY_BLEND,    // the GS's blending in the shader (DATE, partial masks, PABE, no COLCLAMP, FBA by As ...)
    SA_WHY_OWN,      // its texture is the frame it draws into
    SA_WHY_SAMPLES,  // multisampled, on a GPU without sample-rate shading (the copies back need it)
    SA_WHY_COUNT
};
const char *const sa_why_names[SA_WHY_COUNT] = {"other target", "targets", "shader blending", "reads its frame",
                                               "no sample shading"};
enum {
    SA_TEX_TABLE,     // decoded from the uploads that own it
    SA_TEX_TARGET,    // a render target's (the GS's)
    SA_TEX_DRAWN,     // a page of it drawn into or copied over since its upload (the GS's VRAM)
    SA_TEX_UNOWNED,   // a block of it no upload owns (never uploaded since the start, or a save state's)
    SA_TEX_MIXED,     // a block of it owned by more uploads than the table keeps
    SA_TEX_EVICTED,   // an upload of it no longer kept
    SA_TEX_COUNT
};
const char *const sa_tex_names[SA_TEX_COUNT] = {"from the table", "a render target", "drawn or copied over",
                                               "not uploaded", "too many owners", "upload let go"};

struct SaStats {
    uint64_t batches, draws, triangles, shader_blends, acquires, releases, frames_touched;
    uint64_t declined[SA_WHY_COUNT];
    uint64_t textures[SA_TEX_COUNT];
    uint64_t decodes, decode_hits, uploads, upload_bytes, chunks_new, replayed_uploads;
};
SaStats sa_stats;

// ---- Who owns VRAM: the page-ownership table -------------------------------------------------------
// An upload's bytes and where they went (BITBLTBUF, TRXPOS, TRXREG): what an upload function sent
struct SaChunk {
    uint64_t bitbltbuf, trxpos, trxreg;
    std::vector<uint8_t> bytes;
    uint64_t seq;  // the table's count when it was last uploaded
    uint64_t used; // the frame it was last read or uploaded in
    uint64_t hash; // (upload_hash: what it is found again by)
};
const uint32_t SA_BLOCKS = GSN_VRAM_BYTES / 256;
const uint32_t SA_OWNERS = 4;
// A block: the uploads that wrote it since one wrote all of it (in order, oldest first; n 0: none; past
// SA_OWNERS: SA_MANY), and the table's count at the last of them. A page drawn into or copied over after
// that (page_drawn later than seq) is not theirs any more.
const uint8_t SA_MANY = 0xFF;
struct SaBlock {
    uint64_t seq;
    uint8_t n;
    uint32_t id[SA_OWNERS];
};

struct Sa {
    bool on = false;
    // the batch's corners, and the draw being met (nd_register): to the scene or not
    bool pending = false, in_draw = false, draw_on = false;
    uint32_t kinds = (1u << RD_KIND_MAP_RECORD) | (1u << RD_KIND_ITEM); // (KF4_SA_KINDS: a mask of RD_KIND_ bits)
    bool log = false;                 // (KF4_SA_LOG=1: each batch, and why one is declined)
    float pixel[2] = {-1.0f, -1.0f};  // (KF4_SA_PIXEL=X,Y: the batches over that GS pixel)
    // the table
    std::vector<SaBlock> blocks;
    uint64_t page_drawn[512] = {};
    uint64_t seq = 0, frame = 0;
    std::unordered_map<uint32_t, SaChunk> chunks;
    std::unordered_map<uint64_t, uint32_t> chunk_by_hash;
    std::deque<uint32_t> chunk_order;
    size_t chunk_bytes = 0;
    uint32_t next_id = 1;
    // the uploads of the frame, in gsn.cpp's uploads_now order, for a picture between's asking for them again
    struct Recorded {
        uint64_t bitbltbuf, trxpos, trxreg;
        uint32_t id;
    };
    std::vector<Recorded> uploads_now, uploads_frame;
    int recording = -1;
    std::vector<uint8_t> scratch; // the scratch VRAM a texture is decoded in
    // decoded textures by what they came from (sa_texture_key)
    struct Decoded {
        ImageHandle image;
        std::array<uint32_t, 256> texels;
    };
    std::unordered_map<uint64_t, Decoded> decoded;
    std::deque<uint64_t> decoded_order;
    size_t decoded_bytes = 0;
    // the scene's targets, and the GS's frame and Z buffer it has taken (bound)
    ImageHandle color, depth;
    ImageHandle copy; // (shader blending's copy of the scene's colour)
    uint32_t samples = 0;
    bool bound = false;
    uint32_t fbp = 0, fbw = 0, fpsm = 0, zbp = 0, zpsm = 0;
    Target *frame_target = nullptr, *z_target = nullptr;
    uint32_t rows0 = 0, rows1 = 0; // the rows the scene drew into since it took them (GS pixels)
    bool z_written = false;
    // programs
    Shader *vs = nullptr;
    Program *program = nullptr, *program_sample = nullptr, *program_copy_ms = nullptr;
    Program *program_depth = nullptr, *program_depth_ms = nullptr;
} sa;

void sa_release();

// The blocks of a rectangle of a buffer (bp, bw, psm), x0..x0+w by y0..y0+h, each with whether the
// rectangle covers all of it
uint32_t sa_block_w(uint32_t psm) {
    return psm == GSN_PSMT4 ? 32 : psm == GSN_PSMT8 || psm_is_16(psm) ? 16 : 8;
}
uint32_t sa_block_h(uint32_t psm) {
    return psm == GSN_PSMT8 || psm == GSN_PSMT4 ? 16 : 8;
}
template <class F> void sa_blocks_of(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                     F &&f) {
    if (!gsn_psm_valid(psm) || w == 0 || h == 0) {
        return;
    }
    uint32_t bx = sa_block_w(psm), by = sa_block_h(psm);
    // (formats that own part of each word, and 24-bit ones, never cover a block)
    bool whole = psm != GSN_PSMCT24 && psm != GSN_PSMZ24 && psm != GSN_PSMT8H && psm != GSN_PSMT4HL && psm != GSN_PSMT4HH;
    uint32_t xa = x0 / bx * bx, ya = y0 / by * by;
    for (uint32_t y = ya; y < y0 + h; y += by) {
        for (uint32_t x = xa; x < x0 + w; x += bx) {
            uint32_t block = (gsn_vram_offset(psm, bp, bw, x & 2047, y & 2047, nullptr) / 256) % SA_BLOCKS;
            bool all = whole && x >= x0 && x + bx <= x0 + w && y >= y0 && y + by <= y0 + h;
            f(block, all);
        }
    }
}

void sa_table_reset() {
    sa.blocks.assign(SA_BLOCKS, SaBlock{0, 0, {0, 0, 0, 0}});
    memset(sa.page_drawn, 0, sizeof(sa.page_drawn));
    sa.uploads_now.clear();
    sa.uploads_frame.clear();
    sa.recording = -1;
}

// (mark_dirty) Rows y0..y1-1 of a target drawn into: its pages are not their uploads' any more
void sa_drawn(const Target &t, uint32_t y0, uint32_t y1) {
    if (!sa.on || y1 <= y0) {
        return;
    }
    uint32_t lo, count;
    buffer_pages(t.bp, t.bw, t.psm, t.width, std::min(y1, t.height), &lo, &count);
    uint32_t first_row = y0 / page_height(t.psm);
    uint32_t across = std::max(std::max(t.bw, 1u) * 64 / page_width(t.psm), 1u);
    sa.seq++;
    for (uint32_t k = std::min(first_row * across, count); k < count; k++) {
        sa.page_drawn[(lo + k) % vram_pages] = sa.seq;
    }
}

// (begin_transfer) A local-to-local copy's destination: its pages are not their uploads' any more
void sa_copied(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg) {
    if (!sa.on) {
        return;
    }
    uint32_t lo, count;
    buffer_pages(bits(bitbltbuf, 32, 14), bits(bitbltbuf, 48, 6), bits(bitbltbuf, 56, 6),
                 bits(trxpos, 32, 11) + bits(trxreg, 0, 12), bits(trxpos, 48, 11) + bits(trxreg, 32, 12), &lo, &count);
    sa.seq++;
    for (uint32_t k = 0; k < count && k < vram_pages; k++) {
        sa.page_drawn[(lo + k) % vram_pages] = sa.seq;
    }
}

// The chunk a block list names, if still kept
SaChunk *sa_chunk(uint32_t id) {
    auto it = sa.chunks.find(id);
    return it == sa.chunks.end() ? nullptr : &it->second;
}

// The upload's blocks are its own (all of a block it covers whole; added to the others of one it
// covers in part)
void sa_own(uint32_t id, SaChunk &c) {
    sa.seq++;
    c.seq = sa.seq;
    uint64_t bitbltbuf = c.bitbltbuf;
    sa_blocks_of(bits(bitbltbuf, 32, 14), bits(bitbltbuf, 48, 6), bits(bitbltbuf, 56, 6), bits(c.trxpos, 32, 11),
                 bits(c.trxpos, 48, 11), bits(c.trxreg, 0, 12), bits(c.trxreg, 32, 12), [&](uint32_t block, bool all) {
                     SaBlock &b = sa.blocks[block];
                     uint32_t page = block / 32;
                     bool stale = sa.page_drawn[page] > b.seq; // (drawn over since: what it had is gone)
                     if (all || stale || b.n == 0) {
                         b.n = 1;
                         b.id[0] = id;
                     } else if (b.n != SA_MANY) {
                         // (the same upload again goes last)
                         uint32_t n = 0;
                         for (uint32_t i = 0; i < b.n; i++) {
                             if (b.id[i] != id) {
                                 b.id[n++] = b.id[i];
                             }
                         }
                         if (n < SA_OWNERS) {
                             b.id[n++] = id;
                             b.n = uint8_t(n);
                         } else {
                             b.n = SA_MANY;
                         }
                     }
                     b.seq = sa.seq;
                 });
}

// (complete_upload, upload_done) An upload whole: its bytes kept (once: found again by their hash, an
// upload sent every frame is the same chunk), its blocks its own
void sa_uploaded(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, const uint8_t *data, size_t size) {
    if (!sa.on) {
        return;
    }
    sa_stats.uploads++;
    sa_stats.upload_bytes += size;
    PendingUpload p;
    p.bitbltbuf = bitbltbuf;
    p.trxpos = trxpos;
    p.trxreg = trxreg;
    uint64_t hash = upload_hash(p, data, size);
    uint32_t id = 0;
    auto found = sa.chunk_by_hash.find(hash);
    if (found != sa.chunk_by_hash.end()) {
        SaChunk *c = sa_chunk(found->second);
        if (c && c->bitbltbuf == bitbltbuf && c->trxpos == trxpos && c->trxreg == trxreg && c->bytes.size() == size &&
            memcmp(c->bytes.data(), data, size) == 0) {
            id = found->second;
        }
    }
    if (!id) {
        id = sa.next_id++;
        if (!sa.next_id) {
            sa.next_id = 1;
        }
        SaChunk &c = sa.chunks[id];
        c.bitbltbuf = bitbltbuf;
        c.trxpos = trxpos;
        c.trxreg = trxreg;
        c.bytes.assign(data, data + size);
        c.hash = hash;
        c.used = sa.frame;
        sa.chunk_by_hash[hash] = id;
        sa.chunk_order.push_back(id);
        sa.chunk_bytes += size;
        sa_stats.chunks_new++;
        // the oldest not used for two frames let go beyond 256 MB (a block naming one gone is the GS's); those
        // in use go to the back, each looked at once at most
        for (size_t tries = sa.chunk_order.size(); sa.chunk_bytes > (256u << 20) && tries > 0; tries--) {
            uint32_t old = sa.chunk_order.front();
            sa.chunk_order.pop_front();
            SaChunk *o = sa_chunk(old);
            if (!o) {
                continue;
            }
            if (o->used + 2 >= sa.frame) {
                sa.chunk_order.push_back(old);
                continue;
            }
            sa.chunk_bytes -= o->bytes.size();
            auto h = sa.chunk_by_hash.find(o->hash);
            if (h != sa.chunk_by_hash.end() && h->second == old) {
                sa.chunk_by_hash.erase(h);
            }
            sa.chunks.erase(old);
        }
    }
    SaChunk &c = sa.chunks[id];
    c.used = sa.frame;
    sa_own(id, c);
    if (sa.recording >= 0 && size_t(sa.recording) < sa.uploads_now.size()) {
        sa.uploads_now[size_t(sa.recording)].id = id;
    }
    sa.recording = -1;
}

// (upload_done) An upload written as it came (settle_upload): its bytes read back from VRAM
void sa_uploaded_from_vram(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg) {
    if (!sa.on) {
        return;
    }
    uint32_t psm = bits(bitbltbuf, 56, 6);
    size_t qwords = (size_t(bits(trxreg, 0, 12)) * bits(trxreg, 32, 12) * gsn_psm_bits(psm) + 127) / 128;
    std::vector<uint8_t> bytes(qwords * 16);
    // (read as a local-to-host transfer of the destination: its source half made the destination's)
    uint64_t as_source = (bitbltbuf & ~uint64_t(0xFFFFFFFF)) | (bitbltbuf >> 32);
    uint64_t pos = (trxpos & ~uint64_t(0xFFFFFFFF)) | (trxpos >> 32);
    gsn_vram_read_image(gs->vram.data(), as_source, pos, trxreg, bytes.data(), qwords);
    sa_uploaded(bitbltbuf, trxpos, trxreg, bytes.data(), bytes.size());
}

// (begin_transfer, an upload: in gsn.cpp's uploads_now when it records one) and (vsync_now) the frame's
void sa_upload_begins(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, bool recorded) {
    if (!sa.on) {
        return;
    }
    sa.recording = -1;
    if (recorded) {
        sa.uploads_now.push_back({bitbltbuf, trxpos, trxreg, 0});
        sa.recording = int(sa.uploads_now.size() - 1);
    }
}
void sa_vblank(bool extra) {
    if (!sa.on) {
        return;
    }
    if (!extra) {
        sa.frame++;
        if (!sa.uploads_now.empty()) {
            sa.uploads_frame.swap(sa.uploads_now);
            sa.uploads_now.clear();
            sa.recording = -1;
        }
    }
}
// (replay_upload) A picture between asks for the frame's k-th upload again: its blocks its own again
void sa_upload_replayed(uint32_t k) {
    if (!sa.on || k >= sa.uploads_frame.size()) {
        return;
    }
    const Sa::Recorded &r = sa.uploads_frame[k];
    SaChunk *c = r.id ? sa_chunk(r.id) : nullptr;
    if (c && c->bitbltbuf == r.bitbltbuf && c->trxpos == r.trxpos && c->trxreg == r.trxreg) {
        sa_own(r.id, *c);
        sa_stats.replayed_uploads++;
    } else {
        // (not known: its blocks the GS's)
        uint32_t lo, count;
        buffer_pages(bits(r.bitbltbuf, 32, 14), bits(r.bitbltbuf, 48, 6), bits(r.bitbltbuf, 56, 6),
                     bits(r.trxpos, 32, 11) + bits(r.trxreg, 0, 12), bits(r.trxpos, 48, 11) + bits(r.trxreg, 32, 12), &lo,
                     &count);
        sa.seq++;
        for (uint32_t i = 0; i < count && i < vram_pages; i++) {
            sa.page_drawn[(lo + i) % vram_pages] = sa.seq;
        }
    }
}

// The uploads a buffer's rectangle comes from, oldest first, or why not all of it does (SA_TEX_*)
int sa_owners(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h, std::vector<uint32_t> &ids) {
    int why = SA_TEX_TABLE;
    sa_blocks_of(bp, bw, psm, 0, 0, w, h, [&](uint32_t block, bool) {
        if (why != SA_TEX_TABLE) {
            return;
        }
        const SaBlock &b = sa.blocks[block];
        if (b.n == 0) {
            why = SA_TEX_UNOWNED;
        } else if (b.n == SA_MANY) {
            why = SA_TEX_MIXED;
        } else if (sa.page_drawn[block / 32] > b.seq) {
            why = SA_TEX_DRAWN;
        } else {
            for (uint32_t i = 0; i < b.n; i++) {
                if (std::find(ids.begin(), ids.end(), b.id[i]) == ids.end()) {
                    ids.push_back(b.id[i]);
                }
            }
        }
    });
    for (uint32_t id : ids) {
        if (why == SA_TEX_TABLE && !sa_chunk(id)) {
            why = SA_TEX_EVICTED;
        }
    }
    return why;
}

// ---- Textures from their source ------------------------------------------------------------------
struct SaTexture {
    const ImageView *raw_view = nullptr, *target_view = nullptr;
    uint32_t src = SRC_CT32;
    int32_t src_offset[2] = {0, 0};
    uint32_t tscale = 0;
    uint32_t clut[256];
    bool want_clut = false;
    uint32_t tex_alpha_max = 255, tex_lo = 0, tex_hi = 255;
    bool cut_texels = false;
    uint32_t cut_lo = 255, cut_hi = 0;
    uint64_t cut_clear = 0, cut_all = 0;
    bool reads_target = false, reads_own = false;
};

// A texture (TEX0's, its mipmap levels', the part of level 0 the draws read) decoded from the uploads that
// own all of its blocks: written into the scratch VRAM, oldest first, over blocks made zero, and read back
// as decode_texture reads VRAM. Found again by what it is and the uploads it came from. nullptr when not
// all of it is the uploads' (why says why).
const Sa::Decoded *sa_table_texture(uint32_t tbw_psm_tw_th[4], uint32_t tbp, uint32_t levels, const uint32_t *mip_tbp,
                                     const uint32_t *mip_tbw, uint32_t cols, uint32_t rows, int *why) {
    uint32_t tbw = tbw_psm_tw_th[0], psm = tbw_psm_tw_th[1], tw = tbw_psm_tw_th[2], th = tbw_psm_tw_th[3];
    std::vector<uint32_t> ids;
    for (uint32_t l = 0; l < levels; l++) {
        *why = sa_owners(mip_tbp[l], mip_tbw[l], psm, std::max(1u, cols >> l), std::max(1u, rows >> l), ids);
        if (*why != SA_TEX_TABLE) {
            return nullptr;
        }
    }
    // (oldest first: the order they were uploaded in, last time each)
    std::sort(ids.begin(), ids.end(), [](uint32_t a, uint32_t b) { return sa_chunk(a)->seq < sa_chunk(b)->seq; });
    uint64_t key = texture_key(tbp, tbw, psm, tw, th, levels, mip_tbp, mip_tbw, cols, rows);
    for (uint32_t id : ids) {
        key = (key ^ id) * 0x100000001B3ull;
        key ^= key >> 29;
        sa_chunk(id)->used = sa.frame;
    }
    auto found = sa.decoded.find(key);
    if (found != sa.decoded.end()) {
        sa_stats.decode_hits++;
        return &found->second;
    }
    Timer timer(gsn_stats.ns_decode);
    if (sa.scratch.empty()) {
        sa.scratch.assign(GSN_VRAM_BYTES, 0);
    }
    uint8_t *vram = sa.scratch.data();
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t w = std::max(1u, (1u << tw) >> l), h = std::max(1u, (1u << th) >> l);
        sa_blocks_of(mip_tbp[l], mip_tbw[l], psm, 0, 0, w, h, [&](uint32_t block, bool) {
            memset(vram + size_t(block) * 256, 0, 256);
        });
    }
    for (uint32_t id : ids) {
        const SaChunk &c = *sa_chunk(id);
        GsnUpload u;
        gsn_upload_begin(&u, c.bitbltbuf, c.trxpos, c.trxreg);
        gsn_upload_data(&u, vram, c.bytes.data(), c.bytes.size(), nullptr, nullptr);
    }
    VkFormat format = raw_format(psm);
    Sa::Decoded d;
    d.image = make_image(1u << tw, 1u << th, format, levels, false);
    d.texels.fill(0);
    {
        Why why_(GSN_WHY_DECODE);
        end_pass();
    }
    transfer_barrier();
    std::vector<uint32_t> row;
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t w = std::max(1u, (1u << tw) >> l), h = std::max(1u, (1u << th) >> l);
        void *out = cmd().update_image(*d.image, {0, 0, 0}, {w, h, 1}, w, h, {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1});
        row.resize(w);
        for (uint32_t y = 0; y < h; y++) {
            size_t at = size_t(y) * w;
            gsn_vram_read_row(vram, psm, mip_tbp[l], mip_tbw[l], 0, y, w, row.data());
            for (uint32_t x = 0; x < w; x++) {
                if (format == VK_FORMAT_R32_UINT) {
                    static_cast<uint32_t *>(out)[at + x] = row[x];
                    d.texels[row[x] >> 24]++;
                } else if (format == VK_FORMAT_R8_UINT) {
                    static_cast<uint8_t *>(out)[at + x] = uint8_t(row[x]);
                    d.texels[row[x] & 0xFF]++;
                } else {
                    static_cast<uint16_t *>(out)[at + x] = uint16_t(row[x]);
                }
            }
        }
        gsn_stats.texture_bytes += size_t(w) * h;
    }
    transfer_barrier();
    sa_stats.decodes++;
    sa.decoded_bytes += image_bytes(*d.image);
    sa.decoded_order.push_back(key);
    auto &slot = sa.decoded[key] = std::move(d);
    // the oldest let go beyond 2,048 of them or 256 MB (one in use keeps its image all the same)
    while (sa.decoded_order.size() > 2048 || sa.decoded_bytes > (256u << 20)) {
        auto oldest = sa.decoded.find(sa.decoded_order.front());
        if (oldest != sa.decoded.end() && &oldest->second != &slot) {
            sa.decoded_bytes -= image_bytes(*oldest->second.image);
            sa.decoded.erase(oldest);
        }
        sa.decoded_order.pop_front();
    }
    return &slot;
}

// The batch's texture (flush_batch's, "the texture"): from the table where all of it is the uploads';
// else the GS's (a render target, or VRAM)
void sa_texture(SaTexture &st, Push &push, uint32_t ctxt, uint64_t tex0, uint64_t tex1, uint64_t clamp, bool fst,
                const Target *color) {
    uint32_t tbp = bits(tex0, 0, 14), tbw = bits(tex0, 14, 6), tpsm = bits(tex0, 20, 6);
    uint32_t tw = std::min(bits(tex0, 26, 4), 10u), th = std::min(bits(tex0, 30, 4), 10u);
    push.flags |= F_TME | (fst ? F_FST : 0) | (bits(tex0, 35, 2) << F_TFX_SHIFT) | (bits(tex0, 34, 1) ? F_TCC : 0);
    push.tex_size = (1u << tw) | ((1u << th) << 16);
    push.clamp_lo = uint32_t(clamp & 0xFFFFFF);
    push.clamp_hi = bits(clamp, 24, 20);
    push.tex1 = bits(tex1, 5, 1) | (bits(tex1, 6, 3) << 1) | (bits(tex1, 2, 3) << 4) | (bits(tex1, 0, 1) << 7) |
                (bits(tex1, 19, 2) << 8);
    int32_t k = int32_t(bits(tex1, 32, 12) << 20) >> 20;
    push.lod_k = float(k) / 16.0f;
    uint32_t finer = std::min(target_sx, target_sy);
    int mode = lod_mode;
    push.lod_bias = mode == GSN_LOD_FULL ? -64.0f : mode == GSN_LOD_SCALE ? -log2f(float(finer)) : 0.0f;
    push.misc |= uint32_t(aniso_log2) << M_ANISO_SHIFT;
    st.raw_view = &gs->dummy_raw->get_view();
    st.target_view = &gs->dummy_rgba->get_view();

    // the rows and columns the batch reads (flush_batch's rows_read, cols_read; in a picture between all)
    auto read_extent = [&](bool across) -> uint32_t {
        uint32_t n = across ? 1u << tw : 1u << th;
        if (interp.active) {
            return n;
        }
        float lo = 0.0f, hi = 0.0f;
        for (const Vtx &v : gs->verts) {
            float c = across ? (fst ? v.s : v.q > 0.0f ? v.s / v.q * float(n) : -1.0f)
                             : (fst ? v.t : v.q > 0.0f ? v.t / v.q * float(n) : -1.0f);
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
        return lo >= 0.0f && hi < float(n) ? std::max(1u, uint32_t(ceilf(hi))) : n;
    };
    uint32_t levels = 1;
    uint32_t mip_tbp[7] = {tbp}, mip_tbw[7] = {tbw};
    uint32_t mmin = bits(tex1, 6, 3), mxl = bits(tex1, 2, 3);
    if (mmin >= 2 && mmin <= 5 && mxl > 0) {
        levels = std::min(mxl, 6u) + 1;
        uint64_t m1 = gs->reg[R_MIPTBP1_1 + ctxt], m2 = gs->reg[R_MIPTBP2_1 + ctxt];
        for (uint32_t l = 1; l < levels; l++) {
            uint64_t m = l <= 3 ? m1 : m2;
            uint32_t at = ((l - 1) % 3) * 20;
            mip_tbp[l] = bits(m, at, 14);
            mip_tbw[l] = bits(m, at + 14, 6);
        }
        levels = std::min(levels, std::max(tw, th) + 1);
    }
    uint32_t cols = 1u << tw, rows = 1u << th;
    if (levels == 1 && bits(clamp, 0, 2) != 3) {
        uint32_t pw = page_width(tpsm);
        cols = std::min(cols, (read_extent(true) + pw - 1) / pw * pw);
    }
    if (levels == 1 && bits(clamp, 2, 2) != 3) {
        uint32_t ph = page_height(tpsm);
        rows = std::min(rows, (read_extent(false) + ph - 1) / ph * ph);
    }

    // from the table
    int why = SA_TEX_TARGET;
    uint32_t shape[4] = {tbw, tpsm, tw, th};
    static int no_table = -1;
    if (no_table < 0) {
        no_table = getenv("KF4_SA_NO_TABLE") && atoi(getenv("KF4_SA_NO_TABLE")) > 0; // (tests: the GS's textures)
    }
    const Sa::Decoded *dec = no_table ? nullptr : sa_table_texture(shape, tbp, levels, mip_tbp, mip_tbw, cols, rows, &why);
    const uint32_t *texels = nullptr;
    if (dec) {
        st.raw_view = &dec->image->get_view();
        texels = dec->texels.data();
    } else {
        // the GS's: a render target (flush_batch's search), else VRAM
        Target *source = nullptr;
        for (auto &t : gs->targets) {
            if (t->depth || tbw != t->bw || tbp < t->bp) {
                continue;
            }
            uint32_t blocks = tbp - t->bp;
            if (psm_layout(tpsm) != psm_layout(t->psm) || blocks % 32 != 0) {
                continue;
            }
            uint32_t page = blocks / 32, across = std::max(t->bw, 1u);
            uint32_t x0 = (page % across) * page_width(t->psm), y0 = (page / across) * page_height(t->psm);
            if (y0 >= t->height) {
                continue;
            }
            if (y0 + read_extent(false) > t->height) {
                break;
            }
            source = t.get();
            st.src_offset[0] = int32_t(x0);
            st.src_offset[1] = int32_t(y0);
            st.tscale = t->sx | t->sy << 16;
            break;
        }
        if (source) {
            why = SA_TEX_TARGET;
            st.src = tpsm == GSN_PSMCT24 ? SRC_TARGET_CT24 : psm_is_16(tpsm) ? SRC_TARGET_CT16 : SRC_TARGET_CT32;
            st.reads_target = true;
            if (source == color) {
                st.reads_own = true;
                return;
            }
            Why why_(GSN_WHY_TEXTURE);
            msaa_settle(*source);
            st.target_view = &source->image->get_view();
        } else {
            for (uint32_t l = 0; l < levels; l++) {
                uint32_t lo, count;
                buffer_pages(mip_tbp[l], mip_tbw[l], tpsm, std::max(1u, cols >> l), std::max(1u, rows >> l), &lo, &count);
                claim_pages(lo, count, nullptr, false);
            }
            Texture *tex = get_texture(tbp, tbw, tpsm, tw, th, levels, mip_tbp, mip_tbw, cols, rows);
            st.raw_view = &tex->image->get_view();
            texels = tex->texels;
        }
        sa_stats.textures[why]++;
    }
    if (dec) {
        sa_stats.textures[SA_TEX_TABLE]++;
    }
    if (!st.reads_target) {
        st.src = psm_is_palette(tpsm) ? SRC_CLUT : tpsm == GSN_PSMCT24 ? SRC_CT24 : psm_is_16(tpsm) ? SRC_CT16 : SRC_CT32;
        // (the CLUT is the GS's buffer, as TEX0's CLD loaded it from VRAM: those blocks' uploads)
        if (st.src == SRC_CLUT) {
            clut_colours(tex0, st.clut);
            st.want_clut = true;
            st.tex_alpha_max = 0;
            for (uint32_t i = 0; i < 256; i++) {
                st.tex_alpha_max = std::max(st.tex_alpha_max, st.clut[i] >> 24);
            }
        }
        if (texels && (st.src == SRC_CLUT || st.src == SRC_CT32)) {
            st.tex_lo = 255;
            st.tex_hi = 0;
            for (uint32_t i = 0; i < 256; i++) {
                if (texels[i]) {
                    uint32_t a = st.src == SRC_CLUT ? st.clut[i] >> 24 : i;
                    st.tex_lo = std::min(st.tex_lo, a);
                    st.tex_hi = std::max(st.tex_hi, a);
                }
            }
            if (cutout_aa_mode != GSN_CUTOUT_AA_OFF && target_samples > 1) {
                st.cut_texels = true;
                st.cut_lo = st.tex_lo;
                st.cut_hi = st.tex_hi;
                for (uint32_t i = 0; i < 256; i++) {
                    st.cut_all += texels[i];
                    st.cut_clear += (st.src == SRC_CLUT ? st.clut[i] >> 24 : i) == st.cut_lo ? texels[i] : 0;
                }
            }
        }
    }
    push.flags |= st.src << F_SRC_SHIFT;
    push.src_offset[0] = st.src_offset[0];
    push.src_offset[1] = st.src_offset[1];
    push.tscale = st.tscale;
}

// ---- The scene's targets, and its turns with the GS's ------------------------------------------------
ImageHandle sa_image(uint32_t width, uint32_t height, VkFormat format, uint32_t samples) {
    bool depth = format == VK_FORMAT_D32_SFLOAT;
    ImageCreateInfo info = ImageCreateInfo::render_target(width, height, format);
    info.samples = VkSampleCountFlagBits(samples);
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    info.layout = ImageLayout::General;
    info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
    return gs->device->create_image(info, nullptr);
}

void sa_programs() {
    if (sa.vs) {
        return;
    }
    ResourceLayout vert = {};
    vert.input_mask = (1u << 2) | (1u << 4);
    vert.push_constant_size = sizeof(Push);
    vert.sets[1].storage_buffer_mask = 1u;
    vert.sets[1].meta[0].array_size = 1;
    vert.sets[2].storage_buffer_mask = 3u;
    vert.sets[2].meta[0].array_size = 1;
    vert.sets[2].meta[1].array_size = 1;
    sa.vs = gs->device->request_shader(gsn_spv_sa_000_vert, sizeof(gsn_spv_sa_000_vert), &vert);
    sa.program = gs->device->request_program(sa.vs, gs->program->get_shader(ShaderStage::Fragment));
    ResourceLayout frag = {};
    frag.output_mask = 1;
    frag.push_constant_size = sizeof(Push);
    frag.sets[0].sampled_image_mask = (1u << 0) | (1u << 1) | (1u << 3);
    frag.sets[0].uniform_buffer_mask = 1u << 2;
    frag.sets[0].fp_mask = (1u << 1) | (1u << 3);
    for (int b : {0, 1, 2, 3}) {
        frag.sets[0].meta[b].array_size = 1;
    }
    if (sample_shading) {
        sa.program_sample = gs->device->request_program(
            sa.vs, gs->device->request_shader(gsn_spv_sa_sample_frag, sizeof(gsn_spv_sa_sample_frag), &frag));
    }
    ResourceLayout copy_vert = {};
    ResourceLayout copy = {};
    copy.sets[0].sampled_image_mask = 1u;
    copy.sets[0].fp_mask = 1u;
    copy.sets[0].meta[0].array_size = 1;
    Shader *copy_vs = gs->device->request_shader(gsn_spv_msaa_vert, sizeof(gsn_spv_msaa_vert), &copy_vert);
    ResourceLayout colour = copy;
    colour.output_mask = 1;
    if (sample_shading) {
        sa.program_copy_ms = gs->device->request_program(
            copy_vs, gs->device->request_shader(gsn_spv_sa_copy_ms_frag, sizeof(gsn_spv_sa_copy_ms_frag), &colour));
    }
    ResourceLayout depth = copy;
    depth.push_constant_size = 2 * sizeof(float);
    sa.program_depth =
        gs->device->request_program(copy_vs, gs->device->request_shader(gsn_spv_sa_depth_frag, sizeof(gsn_spv_sa_depth_frag), &depth));
    if (sample_shading) {
        sa.program_depth_ms = gs->device->request_program(
            copy_vs, gs->device->request_shader(gsn_spv_sa_depth_ms_frag, sizeof(gsn_spv_sa_depth_ms_frag), &depth));
    }
}

// (flush_batch's) The frame and Z buffer a batch draws into, made as big as each other
bool sa_targets(uint32_t fbp, uint32_t fbw, uint32_t fpsm, uint32_t zbp, uint32_t zpsm, uint32_t need_h, Target **pc,
                Target **pz) {
    Target *color = get_target(fbp, fbw, fpsm, fbw * 64, need_h);
    Target *depth = nullptr;
    uint32_t w = color->width, h = color->height;
    for (int tries = 0; tries < 4; tries++) {
        depth = get_target(zbp, fbw, zpsm, w, h);
        w = std::max(w, depth->width);
        h = std::max(h, depth->height);
        color = get_target(fbp, fbw, fpsm, w, h);
        if (find_target(zbp, fbw, zpsm) == depth && color->width == depth->width && color->height == depth->height) {
            break;
        }
        depth = find_target(zbp, fbw, zpsm);
        if (!depth) {
            continue;
        }
        w = std::max(color->width, depth->width);
        h = std::max(color->height, depth->height);
    }
    if (find_target(zbp, fbw, zpsm) != depth || find_target(fbp, fbw, fpsm) != color || !depth ||
        color->image->get_width() != depth->image->get_width() || color->image->get_height() != depth->image->get_height()) {
        return false;
    }
    *pc = color;
    *pz = depth;
    return true;
}

// The scene takes the frame's picture and Z buffer (all of them: what it draws over blends with them, and is
// tested against them)
bool sa_acquire(uint32_t fbp, uint32_t fbw, uint32_t fpsm, uint32_t zbp, uint32_t zpsm, uint32_t need_h) {
    if (sa.bound && sa.fbp == fbp && sa.fbw == fbw && sa.fpsm == fpsm && sa.zbp == zbp && sa.zpsm == zpsm &&
        find_target(fbp, fbw, fpsm) == sa.frame_target && find_target(zbp, fbw, zpsm) == sa.z_target &&
        sa.frame_target->height >= need_h) {
        return true;
    }
    sa_release();
    Target *color, *depth;
    if (!sa_targets(fbp, fbw, fpsm, zbp, zpsm, need_h, &color, &depth)) {
        return false;
    }
    sa_programs();
    uint32_t w = color->image->get_width(), h = color->image->get_height();
    uint32_t samples = target_samples;
    if (!sa.color || sa.color->get_width() != w || sa.color->get_height() != h || sa.samples != samples ||
        sa.color->get_format() != color->image->get_format()) {
        sa.color = sa_image(w, h, color->image->get_format(), samples);
        sa.depth = sa_image(w, h, VK_FORMAT_D32_SFLOAT, samples);
        sa.samples = samples;
    }
    {
        Why why(GSN_WHY_COPY);
        if (samples > 1) {
            msaa_ready(*color);
            msaa_ready(*depth);
        } else {
            msaa_settle(*color);
            msaa_settle(*depth);
        }
        end_pass();
    }
    gpu_mark(GPU_OTHER);
    transfer_barrier();
    const Image &cs = samples > 1 ? *color->ms : *color->image;
    const Image &zs = samples > 1 ? *depth->ms : *depth->image;
    cmd().copy_image(*sa.color, cs, {0, 0, 0}, {0, 0, 0}, {w, h, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
    cmd().copy_image(*sa.depth, zs, {0, 0, 0}, {0, 0, 0}, {w, h, 1}, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                     {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1});
    transfer_barrier();
    gpu_mark(GPU_COPY);
    gsn_stats.copy_bytes += uint64_t(w) * h * (wide(cs) ? 12 : 8) * samples;
    sa.bound = true;
    sa.fbp = fbp;
    sa.fbw = fbw;
    sa.fpsm = fpsm;
    sa.zbp = zbp;
    sa.zpsm = zpsm;
    sa.frame_target = color;
    sa.z_target = depth;
    sa.rows0 = UINT32_MAX;
    sa.rows1 = 0;
    sa.z_written = false;
    sa_stats.acquires++;
    return true;
}

// One of the copies back (shaders/msaa.vert): src read at each pixel (or sample) of dst, over rows y0..y1
void sa_copy_pass(const Image &dst, const Image &src, Program *program, bool depth, uint32_t y0, uint32_t y1,
                  const void *push, size_t push_size) {
    end_pass();
    RenderPassInfo rp;
    RenderPassInfo::Subpass subpass;
    if (depth) {
        rp.depth_stencil = &dst.get_view();
        rp.op_flags = RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
    } else {
        rp.num_color_attachments = 1;
        rp.color_attachments[0] = &dst.get_view();
        rp.load_attachments = 1;
        rp.store_attachments = 1;
    }
    if (dst.get_create_info().samples != VK_SAMPLE_COUNT_1_BIT) {
        msaa_subpass(rp, subpass);
    }
    rp.render_area = {{0, int32_t(y0)}, {dst.get_width(), y1 - y0}};
    Cmd &c = cmd();
    full_barrier();
    c.begin_render_pass(rp);
    c.set_opaque_state();
    c.set_program(program);
    c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    c.set_cull_mode(VK_CULL_MODE_NONE);
    c.set_depth_test(depth, depth);
    c.set_depth_compare(VK_COMPARE_OP_ALWAYS);
    VkViewport vp = {0.0f, 0.0f, float(dst.get_width()), float(dst.get_height()), 0.0f, 1.0f};
    c.set_viewport(vp);
    c.set_scissor(rp.render_area);
    c.set_texture(0, 0, src.get_view(), StockSampler::NearestClamp);
    if (push) {
        c.push_constants(push, 0, push_size);
    }
    c.draw(3);
    c.end_render_pass();
    gsn_stats.passes++;
    barrier_fresh = false;
    full_barrier();
}

// The scene gives the frame's picture and Z buffer back to the GS: its colour over the rows it drew into,
// and its depth rounded to the GS's whole Z
void sa_release() {
    if (!sa.bound) {
        return;
    }
    sa.bound = false;
    sa_stats.releases++;
    {
        Why why(GSN_WHY_COPY);
        end_pass();
    }
    if (sa.rows1 <= sa.rows0) {
        return; // (nothing drawn: the GS's are as they were)
    }
    sa_stats.frames_touched++;
    Target *color = find_target(sa.fbp, sa.fbw, sa.fpsm), *depth = find_target(sa.zbp, sa.fbw, sa.zpsm);
    if (color != sa.frame_target || depth != sa.z_target || !color || !depth ||
        color->image->get_width() != sa.color->get_width() || color->image->get_height() != sa.color->get_height()) {
        // (the GS let a target go while the scene had it: a texture's pages claimed. Made again from VRAM; the
        // scene's picture is all of it)
        if (!sa_targets(sa.fbp, sa.fbw, sa.fpsm, sa.zbp, sa.zpsm, sa.color->get_height() / std::max(target_sy, 1u), &color,
                        &depth) ||
            color->image->get_width() != sa.color->get_width() || color->image->get_height() != sa.color->get_height()) {
            static int told;
            if (!told++) {
                fprintf(stderr, "[gsn] standalone: the frame target went while the scene had it; its picture is lost\n");
            }
            return;
        }
    }
    uint32_t sy = color->sy;
    uint32_t y0 = std::min(sa.rows0 * sy, sa.color->get_height()), y1 = std::min(sa.rows1 * sy, sa.color->get_height());
    uint32_t w = sa.color->get_width();
    // (--native-layer-z tolerant keeps Z's fraction in the GS's Z buffer too: copied as it is, within the format)
    float zpush[2] = {float(depth_scale(sa.zpsm)) * (layer_z_mode == GSN_LAYER_Z_TOLERANT ? -1.0f : 1.0f),
                      float(depth_max(sa.zpsm))};
    gpu_mark(GPU_OTHER);
    if (sa.samples > 1) {
        msaa_ready(*color);
        msaa_ready(*depth);
        sa_copy_pass(*color->ms, *sa.color, sa.program_copy_ms, false, y0, y1, nullptr, 0);
        if (sa.z_written) {
            sa_copy_pass(*depth->ms, *sa.depth, sa.program_depth_ms, true, y0, y1, zpush, sizeof(zpush));
        }
        color->ms_newer = true;
        color->image_newer = false;
        if (sa.z_written) {
            depth->ms_newer = true;
            depth->image_newer = false;
        }
    } else {
        msaa_settle(*color);
        msaa_settle(*depth);
        transfer_barrier();
        cmd().copy_image(*color->image, *sa.color, {0, int32_t(y0), 0}, {0, int32_t(y0), 0}, {w, y1 - y0, 1},
                         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
        transfer_barrier();
        if (sa.z_written) {
            sa_copy_pass(*depth->image, *sa.depth, sa.program_depth, true, y0, y1, zpush, sizeof(zpush));
        }
        msaa_image_written(*color);
        if (sa.z_written) {
            msaa_image_written(*depth);
        }
    }
    gpu_mark(GPU_MS_COPY);
    gsn_stats.copy_bytes += uint64_t(w) * (y1 - y0) * 8 * sa.samples;
    mark_dirty(*color, sa.rows0, sa.rows1);
    if (sa.z_written) {
        mark_dirty(*depth, sa.rows0, sa.rows1);
    }
}

// The scene's render pass, unless it is under way
void sa_begin_pass() {
    if (gs->in_pass && gs->pass_color == &SA_MARKER) {
        return;
    }
    {
        Why why(GSN_WHY_TARGET);
        end_pass();
    }
    gpu_mark(GPU_OTHER);
    RenderPassInfo rp;
    rp.num_color_attachments = 1;
    rp.color_attachments[0] = &sa.color->get_view();
    rp.load_attachments = 1;
    rp.store_attachments = 1;
    rp.depth_stencil = &sa.depth->get_view();
    rp.op_flags = RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
    RenderPassInfo::Subpass subpass;
    if (sa.samples > 1) {
        msaa_subpass(rp, subpass);
    }
    full_barrier();
    cmd().begin_render_pass(rp);
    gs->in_pass = true;
    gs->pass_color = const_cast<Target *>(&SA_MARKER);
    gs->pass_depth = const_cast<Target *>(&SA_MARKER);
    gs->pass_ms = sa.samples > 1;
    gs->pass_scratch = false;
    gs->pass_area = false;
    gsn_stats.passes++;
}

// (sa_draw_batch) whether the draw writes Z at all
bool zwrite_any(uint64_t zbuf, uint64_t test) {
    return !bits(zbuf, 32, 1) && bits(test, 16, 1);
}

// ---- A batch drawn into the scene ----------------------------------------------------------------------
// (flush_batch, a batch of the scene's corners) Its GS state as flush_batch works it out, drawn with
// sa_000.vert into the scene's targets. False: declined (sa_stats.declined[*why]), for the GS to draw.
bool sa_draw_batch(int *why) {
    uint32_t prim = gs->batch_prim;
    uint32_t ctxt = (prim >> 9) & 1;
    bool tme = (prim >> 4) & 1, fge = (prim >> 5) & 1, abe = (prim >> 6) & 1, fst = (prim >> 8) & 1;
    uint64_t frame = gs->reg[R_FRAME_1 + ctxt], zbuf = gs->reg[R_ZBUF_1 + ctxt], test = gs->reg[R_TEST_1 + ctxt];
    uint64_t alpha = gs->reg[R_ALPHA_1 + ctxt], scissor = gs->reg[R_SCISSOR_1 + ctxt];
    uint64_t tex0 = gs->reg[R_TEX0_1 + ctxt], tex1 = gs->reg[R_TEX1_1 + ctxt], clamp = gs->reg[R_CLAMP_1 + ctxt];
    uint32_t fbp = bits(frame, 0, 9) * 32, fbw = bits(frame, 16, 6), fpsm = bits(frame, 24, 6);
    uint32_t fbmsk = uint32_t(frame >> 32);
    uint32_t zbp = bits(zbuf, 0, 9) * 32, zpsm = bits(zbuf, 24, 4) | 0x30;
    bool zmsk = bits(zbuf, 32, 1);
    uint32_t sx0 = bits(scissor, 0, 11), sx1 = bits(scissor, 16, 11), sy0 = bits(scissor, 32, 11), sy1 = bits(scissor, 48, 11);
    bool ate = bits(test, 0, 1);
    uint32_t atst = bits(test, 1, 3), afail = bits(test, 12, 2);
    bool date = bits(test, 14, 1);
    uint32_t ztst = bits(test, 16, 1) ? bits(test, 17, 2) : 1;
    if (!bits(test, 16, 1)) {
        zmsk = true;
    }
    if ((fpsm != GSN_PSMCT32 && fpsm != GSN_PSMCT24) || fbw * 64 < 512 || sx1 < sx0 || sy1 < sy0) {
        *why = SA_WHY_FRAME;
        return false;
    }
    if (target_samples > 1 && !sample_shading) {
        *why = SA_WHY_SAMPLES;
        return false;
    }
    if (fpsm == GSN_PSMCT24) {
        fbmsk |= 0xFF000000u;
    }
    if (ztst == 0) {
        gs->verts.clear(); // (ZTST NEVER: nothing)
        return true;
    }
    if (!sa_acquire(fbp, fbw, fpsm, zbp, zpsm, sy1 + 1)) {
        *why = SA_WHY_TARGET;
        return false;
    }
    Target *color = sa.frame_target;
    if (sa.log) {
        uint32_t zl = UINT32_MAX, zh = 0;
        for (const Vtx &v : gs->verts) {
            zl = std::min(zl, v.z);
            zh = std::max(zh, v.z);
        }
        fprintf(stderr, "[sa] batch %llu: fbp %#x zbp %#x zpsm %#x ztst %u zmsk %d abe %d alpha %#llx test %#llx z %u-%u n %zu\n",
                (unsigned long long)sa_stats.batches, fbp, zbp, zpsm, ztst, zmsk, abe, (unsigned long long)alpha,
                (unsigned long long)test, zl, zh, gs->verts.size());
    }
    uint32_t as_max = 0, as_min = 255;
    float vy0 = gs->verts[0].y, vy1 = vy0;
    for (const Vtx &v : gs->verts) {
        as_max = std::max(as_max, v.rgba >> 24);
        as_min = std::min(as_min, v.rgba >> 24);
        vy0 = std::min(vy0, v.y);
        vy1 = std::max(vy1, v.y);
    }
    if (interp.active) {
        interp_alpha_range(as_min, as_max);
    }

    Push push = {};
    push.inv_size[0] = 1.0f / float(color->width);
    push.inv_size[1] = 1.0f / float(color->height);
    push.zscale = float(depth_scale(zpsm));
    push.zmax = depth_max(zpsm);
    push.test = uint32_t(test);
    push.fogcol = uint32_t(gs->reg[R_FOGCOL]) & 0xFFFFFF;
    push.alpha = bits(alpha, 0, 2) | (bits(alpha, 2, 2) << 2) | (bits(alpha, 4, 2) << 4) | (bits(alpha, 6, 2) << 6) |
                 (bits(alpha, 32, 8) << 8);
    push.texa = uint32_t(gs->reg[R_TEXA] & 0x80FF) | (bits(gs->reg[R_TEXA], 32, 8) << 16);
    push.fbmsk = fbmsk;
    push.misc = (bits(gs->reg[R_FBA_1 + ctxt], 0, 1) ? M_FBA : 0) | (bits(gs->reg[R_COLCLAMP], 0, 1) ? M_COLCLAMP : 0) |
                (fpsm == GSN_PSMCT24 ? M_DST24 : 0) | (bits(gs->reg[R_PABE], 0, 1) ? M_PABE : 0) |
                (bits(gs->reg[R_SCANMSK], 0, 2) << M_SCANMSK_SHIFT);
    if (wide(*sa.color)) {
        push.misc |= M_HIGH;
    }
    push.flags = fge ? F_FGE : 0;

    SaTexture st;
    if (tme) {
        sa_texture(st, push, ctxt, tex0, tex1, clamp, fst, color);
        if (st.reads_own) {
            *why = SA_WHY_OWN;
            return false;
        }
    }

    // blending: Vulkan's, as flush_batch has it; the GS's shader blending is the GS's (declined). (Not for a
    // texture that is a target, which flush_batch blends in the shader to the GS's rounding: that is for the
    // passes that read the frame again and again; the scene's 3D is drawn anew each frame, the lava over its
    // scrolling target a level off at most, as the other 3D's Vulkan blending is.)
    uint32_t ba = bits(alpha, 0, 2), bb = bits(alpha, 2, 2), bc = bits(alpha, 4, 2), bd = bits(alpha, 6, 2);
    uint32_t fix = bits(alpha, 32, 8);
    FixedBlend blend = {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0};
    bool partial_mask = false;
    for (int i = 0; i < 4; i++) {
        uint32_t m = (fbmsk >> (i * 8)) & 0xFF;
        partial_mask |= m != 0 && m != 0xFF;
    }
    bool fixed = fixed_blend(ba, bb, bc, bd, fix, &blend);
    uint32_t tex_alpha_max = st.tex_alpha_max;
    uint32_t as_hi = as_max;
    if (tme && bits(tex0, 34, 1)) {
        uint32_t tpsm = bits(tex0, 20, 6);
        if (tpsm == GSN_PSMCT24) {
            tex_alpha_max = bits(gs->reg[R_TEXA], 0, 8);
        } else if (psm_is_16(tpsm)) {
            tex_alpha_max = std::max(bits(gs->reg[R_TEXA], 0, 8), bits(gs->reg[R_TEXA], 32, 8));
        }
        uint32_t tfx = bits(tex0, 35, 2);
        as_hi = tfx == 0 ? std::min(255u, (as_max * tex_alpha_max) >> 7) : tfx == 2 ? std::min(255u, as_max + tex_alpha_max)
                                                                                   : tex_alpha_max;
    }
    bool as_over = bc == 0 && !blend.premul && as_hi > 0x80;
    // what Vulkan's blending cannot do (DATE, a partial mask, PABE, no COLCLAMP, FBA with As, As above 1.0,
    // Ad or a FIX above 1.0) is blended in the shader, as flush_batch does: from a copy of the scene's colour
    // taken before each run of triangles that do not overlap (gs_frag.h's GSN_F_SHADER_BLEND)
    bool shader_blend = date || partial_mask ||
                        (abe && ((push.misc & M_PABE) || !(push.misc & M_COLCLAMP) || (bc == 0 && (push.misc & M_FBA)) ||
                                 as_over || !fixed));
    if (shader_blend) {
        push.flags |= F_SHADER_BLEND | (date ? F_DATE : 0);
        if (!abe) {
            push.alpha = (2 << 0) | (2 << 2) | (2 << 4) | (0 << 6); // (0 - 0) * FIX + Cs: no blending
        }
        sa_stats.shader_blends++;
    } else if (abe && blend.premul) {
        push.flags |= blend.premul;
    } else if (abe && bc == 0 && !(ba == bb)) {
        push.flags |= F_ALPHA_FACTOR;
    }
    // Ties. On the GS a draw over a surface of (nearly) the same Z wins where the two round to the same Z,
    // and the data counts on it: a layer blended over a wall, a wall's overlay, the darkness in the arches,
    // a sign on its wall, the hills' edge against the lava. The scene keeps Z's fraction, so such a draw
    // would come out a hair in front or behind, in stripes along the two meshes' triangles. So a draw tested
    // GEQUAL (or GREATER) passes within a slack of what is there, as --native-layer-z tolerant has it
    // (gsn.cpp, "Z and layers"): its Z first, exactly (the nearer of its own and what was there), then its
    // colour within the slack, no Z written; a draw that writes no Z in one pass within it. The slack: one
    // Z for a draw that blends by an As not 0x80 everywhere, or adds (the layers, overlays, glows); half a Z
    // for the others (the GS's tie on average). Without the GS's rounding: no sawtooth where surfaces cross,
    // no lace where two meshes' triangles differ.
    uint32_t a_lo = as_min, a_hi = as_max;
    if (tme && bits(tex0, 34, 1)) {
        uint32_t tfx = bits(tex0, 35, 2);
        a_lo = tfx == 0 ? (as_min * st.tex_lo) >> 7 : tfx == 2 ? std::min(255u, as_min + st.tex_lo) : st.tex_lo;
        a_hi = tfx == 0 ? std::min(255u, (as_max * st.tex_hi) >> 7) : tfx == 2 ? std::min(255u, as_max + st.tex_hi) : st.tex_hi;
    }
    bool same_twice = !abe || (ba == 0 && bb == 1 && bd == 1 && (bc == 0 ? a_lo == 0x80 && a_hi == 0x80 : bc == 2 && fix == 0x80));
    static float slack_layer = -1.0f, slack_tie = -1.0f;
    if (slack_layer < 0.0f) {
        const char *v = getenv("KF4_SA_SLACK"); // (tests: the slacks in Z, LAYER[,TIE]; 1,0.5 by default)
        slack_layer = 1.0f;
        slack_tie = 0.5f;
        if (v && *v) {
            slack_layer = float(atof(v));
            const char *comma = strchr(v, ',');
            slack_tie = comma ? float(atof(comma + 1)) : slack_layer;
        }
    }
    bool ties = ztst == 2 || ztst == 3;
    float slack = !zwrite_any(zbuf, test) ? slack_tie : same_twice ? slack_tie : slack_layer;
    // the rows, the scissor; triangles wholly outside it left out (not in a picture between: the GPU moves them)
    uint32_t scale_x = color->sx, scale_y = color->sy;
    uint32_t x1 = std::min(sx1 + 1, color->width), y1 = std::min(sy1 + 1, color->height);
    if (sx0 >= x1 || sy0 >= y1) {
        gs->verts.clear();
        return true;
    }
    if (!interp.active) {
        float cx0 = float(sx0) - 1.0f, cy0 = float(sy0) - 1.0f, cx1 = float(x1) + 1.0f, cy1 = float(y1) + 1.0f;
        size_t kept = 0, n = gs->verts.size();
        Vtx *v = gs->verts.data();
        for (size_t i = 0; i + 2 < n; i += 3) {
            float tx0 = std::min(v[i].x, std::min(v[i + 1].x, v[i + 2].x)), tx1 = std::max(v[i].x, std::max(v[i + 1].x, v[i + 2].x));
            float ty0 = std::min(v[i].y, std::min(v[i + 1].y, v[i + 2].y)), ty1 = std::max(v[i].y, std::max(v[i + 1].y, v[i + 2].y));
            if (tx1 < cx0 || tx0 > cx1 || ty1 < cy0 || ty0 > cy1) {
                continue;
            }
            if (kept != i) {
                v[kept] = v[i];
                v[kept + 1] = v[i + 1];
                v[kept + 2] = v[i + 2];
            }
            kept += 3;
        }
        gs->verts.resize(kept);
        if (!kept) {
            return true;
        }
    }
    push.misc |= scale_y << M_SCALE_SHIFT | scale_x << M_SCALE_X_SHIFT;
    uint32_t write_mask = 0;
    for (int i = 0; i < 4; i++) {
        if (shader_blend || ((fbmsk >> (i * 8)) & 0xFF) != 0xFF) {
            write_mask |= 1u << i;
        }
    }
    if (fpsm == GSN_PSMCT24) {
        write_mask &= 7;
    }

    // the alpha test's passes (flush_batch's), no tie rules
    struct Pass {
        uint32_t select, mask;
        bool zwrite;
    } passes[2];
    int npasses = 0;
    bool zwrite = !zmsk;
    if (!ate || atst == 1) {
        passes[npasses++] = {0, write_mask, zwrite};
    } else {
        if (atst != 0) {
            passes[npasses++] = {1, write_mask, zwrite};
        }
        uint32_t fail_mask = 0;
        bool fail_z = false;
        switch (afail) {
        case 1:
            fail_mask = write_mask;
            break;
        case 2:
            fail_z = zwrite;
            break;
        case 3:
            fail_mask = write_mask & 7;
            break;
        }
        if (fail_mask || fail_z) {
            passes[npasses++] = {atst == 0 ? 0u : 2u, fail_mask, fail_z};
        }
    }
    // --native-cutout-aa: a cut-out shaded at every sample (flush_batch's test)
    bool per_sample = false;
    if (st.cut_texels && bits(tex0, 34, 1) && st.cut_lo < st.cut_hi && st.cut_clear * 4 >= st.cut_all && !fst &&
        cutout_aa_mode == GSN_CUTOUT_AA_SSAA && sa.program_sample) {
        uint32_t tfx = bits(tex0, 35, 2);
        uint32_t lo = tfx == 0 ? (as_min * st.cut_lo) >> 7 : tfx == 2 ? std::min(255u, as_min + st.cut_lo) : st.cut_lo;
        uint32_t hi = tfx == 0 ? std::min(255u, (as_max * st.cut_hi) >> 7) : tfx == 2 ? std::min(255u, as_max + st.cut_hi) : st.cut_hi;
        uint32_t ref = bits(test, 4, 8);
        bool splits = false;
        if (ate) {
            switch (atst) {
            case 2:
            case 5:
                splits = lo < ref && hi >= ref;
                break;
            case 3:
            case 6:
                splits = lo <= ref && hi > ref;
                break;
            case 4:
            case 7:
                splits = lo <= ref && ref <= hi && lo < hi;
                break;
            }
        }
        per_sample = splits || (abe && bc == 0 && ba != bb && lo == 0 && hi > 0);
    }
    if (sa.pixel[0] >= 0.0f) { // (KF4_SA_PIXEL=X,Y, debug: the batches over a GS pixel)
        float qx = sa.pixel[0], qy = sa.pixel[1];
        for (size_t t = 0; t + 2 < gs->verts.size(); t += 3) {
            const Vtx *v = &gs->verts[t];
            float d0 = (v[1].x - v[0].x) * (qy - v[0].y) - (v[1].y - v[0].y) * (qx - v[0].x);
            float d1 = (v[2].x - v[1].x) * (qy - v[1].y) - (v[2].y - v[1].y) * (qx - v[1].x);
            float d2 = (v[0].x - v[2].x) * (qy - v[2].y) - (v[0].y - v[2].y) * (qx - v[2].x);
            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) {
                fprintf(stderr, "[sa px] batch %llu tri %zu: z %u %u %u tex0 %#llx abe %d alpha %#llx test %#llx zbuf %#llx same %d slack %g a %u-%u\n",
                        (unsigned long long)sa_stats.batches, t / 3, v[0].z, v[1].z, v[2].z, (unsigned long long)tex0, abe,
                        (unsigned long long)alpha, (unsigned long long)test, (unsigned long long)zbuf, same_twice, slack, a_lo, a_hi);
            }
        }
    }
    // the ties (above): each pass's Z first, exact, then its colour within the slack
    struct Draw {
        uint32_t select, mask;
        bool zwrite;
        float zbias;
    } draws[4];
    int ndraws = 0;
    for (int i = 0; i < npasses; i++) {
        if (ties && passes[i].zwrite) {
            draws[ndraws++] = {passes[i].select, 0, true, 0.0f};
        }
    }
    for (int i = 0; i < npasses; i++) {
        if (passes[i].mask || !ties) {
            draws[ndraws++] = {passes[i].select, passes[i].mask, !ties && passes[i].zwrite, ties ? slack : 0.0f};
        }
    }
    // two passes over overlapping triangles, or blending in the shader: in runs that do not overlap, so that
    // each sees those before it
    std::vector<uint32_t> runs(1, 0);
    if (npasses > 1 || shader_blend) {
        uint32_t triangles = uint32_t(gs->verts.size() / 3), start = 0;
        for (uint32_t t = 1; t < triangles; t++) {
            bool cut = t - start >= 256;
            for (uint32_t u = start; u < t && !cut; u++) {
                cut = triangles_overlap(&gs->verts[t * 3], &gs->verts[u * 3]);
            }
            if (cut) {
                runs.push_back(t * 3);
                start = t;
            }
        }
    }
    runs.push_back(uint32_t(gs->verts.size()));

    Program *program = per_sample ? sa.program_sample : sa.program;
    size_t bytes = gs->verts.size() * sizeof(Vtx);
    // A layer's colour pass also passes within half its Z's change across a GS pixel more, at most a Z more
    // (--native-layer-z tolerant's slope: at a grazing angle the two meshes, a fraction of a pixel apart, are
    // further apart in Z; a tower's top seen from below). Per triangle, from its corners, in each corner's
    // alpha (which the GPU does not read otherwise): 255 is one Z (sa_000.vert, pc.zslope).
    bool sloped = slack == slack_layer && zwrite_any(zbuf, test) && !same_twice;
    std::vector<Vtx> corners(gs->verts.begin(), gs->verts.end());
    for (size_t t = 0; t + 2 < corners.size(); t += 3) {
        uint32_t extra = 0;
        if (sloped) {
            const Vtx *v = &corners[t];
            float ax = v[1].x - v[0].x, ay = v[1].y - v[0].y, bx = v[2].x - v[0].x, by = v[2].y - v[0].y;
            float az = float(v[1].z) - float(v[0].z), bz = float(v[2].z) - float(v[0].z);
            float det = ax * by - ay * bx;
            if (det != 0.0f) {
                float dzdx = (az * by - bz * ay) / det, dzdy = (bz * ax - az * bx) / det;
                float e = std::min(0.5f * std::max(fabsf(dzdx), fabsf(dzdy)), 1.0f);
                extra = uint32_t(e * 255.0f + 0.5f);
            }
        }
        for (int k = 0; k < 3; k++) {
            corners[t + k].rgba = (corners[t + k].rgba & 0xFFFFFFu) | extra << 24;
        }
    }
    for (size_t r = 0; r + 1 < runs.size(); r++) {
        uint32_t first = runs[r], count = runs[r + 1] - runs[r];
        if (shader_blend) {
            // the run's pixels as they are now, into the copy it blends with
            int32_t box[4] = {0, 0, int32_t(color->width), int32_t(color->height)};
            if (!interp.active) {
                run_box(first, count, box);
            }
            uint32_t W = sa.color->get_width(), H = sa.color->get_height();
            int32_t x0 = std::max(box[0], 0) * int32_t(scale_x), y0 = std::max(box[1], 0) * int32_t(scale_y);
            int32_t bx1 = std::min(box[2] * int32_t(scale_x), int32_t(W)), by1 = std::min(box[3] * int32_t(scale_y), int32_t(H));
            if (!sa.copy || sa.copy->get_width() != W || sa.copy->get_height() != H ||
                sa.copy->get_format() != sa.color->get_format()) {
                sa.copy = sa_image(W, H, sa.color->get_format(), 1);
            }
            if (bx1 > x0 && by1 > y0) {
                Why why_(GSN_WHY_BLEND);
                if (sa.samples > 1) {
                    msaa_resolve(*sa.copy, *sa.color, {x0, y0, 0}, {uint32_t(bx1 - x0), uint32_t(by1 - y0), 1});
                } else {
                    end_pass();
                    transfer_barrier();
                    cmd().copy_image(*sa.copy, *sa.color, {x0, y0, 0}, {x0, y0, 0}, {uint32_t(bx1 - x0), uint32_t(by1 - y0), 1},
                                     {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
                    transfer_barrier();
                }
                gsn_stats.copy_bytes += uint64_t(bx1 - x0) * uint64_t(by1 - y0) * 4;
            }
        }
        bool fresh = !(gs->in_pass && gs->pass_color == &SA_MARKER);
        sa_begin_pass();
        Cmd &c = cmd();
        if (fresh || r == 0) {
            c.set_opaque_state();
            c.set_program(program);
            c.set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            c.set_cull_mode(VK_CULL_MODE_NONE);
            VkViewport vp = {0.0f, 0.0f, float(color->width * scale_x), float(color->height * scale_y), 0.0f, 1.0f};
            c.set_viewport(vp);
            c.set_scissor({{int32_t(sx0 * scale_x), int32_t(sy0 * scale_y)}, {(x1 - sx0) * scale_x, (y1 - sy0) * scale_y}});
            c.set_texture(0, 0, st.raw_view ? *st.raw_view : gs->dummy_raw->get_view(), StockSampler::NearestClamp);
            c.set_texture(0, 1, st.target_view ? *st.target_view : gs->dummy_rgba->get_view(), StockSampler::NearestClamp);
            c.set_texture(0, 3, shader_blend ? sa.copy->get_view() : gs->dummy_rgba->get_view(), StockSampler::NearestClamp);
            uint32_t *clut_data = c.allocate_typed_constant_data<uint32_t>(0, 2, 256);
            if (st.want_clut) {
                memcpy(clut_data, st.clut, sizeof(st.clut));
            } else {
                memset(clut_data, 0, 1024);
            }
            c.set_depth_compare(ztst == 2 ? VK_COMPARE_OP_GREATER_OR_EQUAL : ztst == 3 ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_ALWAYS);
            if (abe && !shader_blend) {
                float constants[4] = {float(fix) / 128.0f, float(fix) / 128.0f, float(fix) / 128.0f, 128.0f / 255.0f};
                c.set_blend_enable(true);
                c.set_blend_constants(constants);
                bool alpha_factor = (push.flags & F_ALPHA_FACTOR) != 0;
                c.set_blend_factors(blend.src, alpha_factor ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE, blend.dst,
                                    VK_BLEND_FACTOR_ZERO);
                c.set_blend_op(blend.op, VK_BLEND_OP_ADD);
            } else {
                c.set_blend_enable(false);
            }
            void *vdata = c.allocate_vertex_data(0, bytes, sizeof(Vtx));
            memcpy(vdata, corners.data(), bytes);
            c.set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vtx, x));
            c.set_vertex_attrib(1, 0, VK_FORMAT_R32_UINT, offsetof(Vtx, z));
            c.set_vertex_attrib(2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Vtx, rgba));
            c.set_vertex_attrib(3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vtx, s));
            c.set_vertex_attrib(4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vtx, fog));
            nd_bind(c);
            c.set_storage_buffer(2, 1, *nd.batch_draws);
        }
        for (int i = 0; i < ndraws; i++) {
            Push p = push;
            p.flags |= draws[i].select << F_PASS_SHIFT;
            p.zbias = draws[i].zbias;
            p.zslope = sloped && draws[i].zbias > 0.0f ? 1.0f : 0.0f;
            c.set_color_write_mask(draws[i].mask);
            c.set_depth_test(true, draws[i].zwrite);
            c.push_constants(&p, 0, sizeof(p));
            c.draw(count, first);
            sa_stats.draws++;
            gsn_stats.draws++;
        }
    }
    sa.z_written |= zwrite;
    // the rows it can have drawn into (a picture between's: anywhere within the scissor)
    uint32_t d0 = interp.active ? sy0 : uint32_t(std::max(float(sy0), std::max(0.0f, floorf(vy0) - 1.0f)));
    uint32_t d1 = interp.active ? y1 : uint32_t(std::min(float(y1), std::max(0.0f, ceilf(vy1) + 2.0f)));
    sa.rows0 = std::min(sa.rows0, d0);
    sa.rows1 = std::max(sa.rows1, d1);
    sa_stats.batches++;
    sa_stats.triangles += gs->verts.size() / 3;
    gsn_stats.primitives += gs->verts.size() / 3;
    gs->verts.clear();
    gs->batch_sprites = false;
    return true;
}

// (flush_batch, at its top, after NdFlush) A batch of the scene's corners is drawn into the scene; anything
// else the GS draws, the scene's turn over first. True: drawn (or nothing to draw).
bool sa_flush() {
    if (!sa.on) {
        return false;
    }
    bool scene = nd.active && !nd.pass_active && !nd.strip_active && (sa.in_draw ? sa.draw_on : sa.pending);
    sa.pending = false;
    if (scene) {
        int why = SA_WHY_FRAME;
        if (sa_draw_batch(&why)) {
            return true;
        }
        sa_stats.declined[why]++;
    }
    sa_release();
    return false;
}

// ---- The draws met (nd_register) -----------------------------------------------------------------------
bool sa_kind(const RdDraw &d) {
    return d.program == 0 && d.kind < 32 && ((sa.kinds >> d.kind) & 1) && (d.flags & ~uint32_t(RD_DRAW_WAVE_UVS | RD_DRAW_FAR_Z)) == 0;
}

// Before a draw is met: whether it goes to the scene; a batch holds the scene's corners or none
void sa_draw_begin(uint64_t value) {
    sa.in_draw = false;
    if (!sa.on) {
        return;
    }
    RdDraw d;
    RdCamera cam;
    static std::vector<std::shared_ptr<const RdMeshData>> meshes;
    bool scene = rd_fetch(value, d, cam, meshes) && sa_kind(d);
    meshes.clear();
    if (!gs->verts.empty() && scene != sa.pending) {
        flush_batch();
    }
    sa.in_draw = true;
    sa.draw_on = scene;
}
void sa_draw_end() {
    if (!sa.on) {
        return;
    }
    sa.in_draw = false;
    sa.pending = !gs->verts.empty() && sa.draw_on;
}

// (nd_kick) A PS2-path kick while the scene's corners wait: they are drawn first
bool sa_waiting() {
    return sa.on && sa.pending && !gs->verts.empty();
}

// (vsync_now, take_snapshot, begin_transfer ...) The GS wants its targets: the scene's turn ends
void sa_sync() {
    if (sa.on) {
        sa_release();
    }
}

// (gsn_set_standalone, at a vblank) on or off
void sa_switch(bool on) {
    if (on == sa.on) {
        return;
    }
    sa_release();
    sa.on = on;
    sa.pending = sa.in_draw = false;
    if (on) {
        sa.log = getenv("KF4_SA_LOG") && atoi(getenv("KF4_SA_LOG")) > 0;
        if (const char *px = getenv("KF4_SA_PIXEL")) {
            sa.pixel[0] = float(atof(px));
            sa.pixel[1] = strchr(px, ',') ? float(atof(strchr(px, ',') + 1)) : sa.pixel[0];
        }
        const char *kinds = getenv("KF4_SA_KINDS");
        if (kinds && *kinds) {
            sa.kinds = uint32_t(strtoul(kinds, nullptr, 0));
        }
        sa_table_reset();
    }
}

// (gsn_state's loading, gsn_restore) VRAM made anew: the scene's turn and the table forgotten
void sa_forget() {
    if (!sa.on) {
        return;
    }
    if (gs->in_pass && gs->pass_color == &SA_MARKER) {
        end_pass();
    }
    sa.bound = false;
    sa.pending = false;
    sa_table_reset();
}

// --stats
void sa_print_stats() {
    if (!sa.on && !sa_stats.batches && !sa_stats.acquires) {
        return;
    }
    const SaStats &s = sa_stats;
    fprintf(stderr,
            "[stats] standalone: %llu batches (%llu triangles, %llu blended in the shader) in %llu draws; the scene took the frame %llu "
            "times, gave it back %llu (%llu drawn into); textures: ",
            (unsigned long long)s.batches, (unsigned long long)s.triangles, (unsigned long long)s.shader_blends,
            (unsigned long long)s.draws, (unsigned long long)s.acquires, (unsigned long long)s.releases,
            (unsigned long long)s.frames_touched);
    for (int i = 0; i < SA_TEX_COUNT; i++) {
        fprintf(stderr, "%s%llu %s", i ? ", " : "", (unsigned long long)s.textures[i], sa_tex_names[i]);
    }
    fprintf(stderr, "; %llu decoded, %llu found again; uploads %llu (%.1f MB, %llu new), %llu asked for again; declined: ",
            (unsigned long long)s.decodes, (unsigned long long)s.decode_hits, (unsigned long long)s.uploads,
            s.upload_bytes / 1048576.0, (unsigned long long)s.chunks_new, (unsigned long long)s.replayed_uploads);
    for (int i = 0; i < SA_WHY_COUNT; i++) {
        fprintf(stderr, "%s%llu %s", i ? ", " : "", (unsigned long long)s.declined[i], sa_why_names[i]);
    }
    fprintf(stderr, "\n");
}
