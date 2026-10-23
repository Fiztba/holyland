// ---- Native draw: a PATH2 strip, the weapon's trail (P2-4) -------------------------------------------
// Included at the end of gsn_native_draw.inc (inside gsn.cpp's namespace). A draw of program
// RD_PROGRAM_GS_STRIP names meshes of RD_VF_GS_STRIP (render.cpp, parse_gs_strip): a GIF packet the game
// built on the CPU, a PACKED triangle strip of RGBAQ and XYZ2 (Player_DrawArm's trail, handed over by
// game/native_draw/nd_arm.c with the arm's identity). No VU1 program is involved: it is written to the GS
// as gif() writes the packet (PRE's PRIM, Q set to 1 by the tag, then each vertex's RGBAQ and XYZ2 or,
// with the ADC bit, XYZ3), so the GS's state, batches and pictures between are the PS2 path's. What a
// draw of its own gives is the identity, for the pictures between (P2-5) and the standalone renderer.

void packed(uint32_t desc, const uint64_t q[2]);

void nd_strip_draw() {
    for (const auto &mesh : nd.fetched) {
        if (mesh->format != RD_VF_GS_STRIP) {
            gsn_stats.nd_unsupported++;
            continue;
        }
        gsn_stats.nd_draws++;
        for (const RdBatch &b : mesh->batches) {
            // (gif(), meeting the tag)
            gs_write(R_PRIM, (b.tag[0] >> 47) & 0x7FF);
            gs->internal_q = 0x3F800000u;
            interp.q_src = GSN_IM_NONE;
            for (uint32_t k = 0; k < b.count; k++) {
                const int32_t *v = &mesh->vertices[size_t(b.first + k) * 8];
                uint64_t q[2];
                memcpy(q, v, 16);
                packed(0x1, q); // RGBAQ
                memcpy(q, v + 4, 16);
                packed(0x5, q); // XYZ2 (XYZ3 with ADC)
            }
            gsn_stats.nd_batches++;
        }
    }
}
