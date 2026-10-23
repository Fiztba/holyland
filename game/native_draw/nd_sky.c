/* nd_sky.c: PC-only. The sky as a kind of native draw of its own (--native-draw sky, PORT_ND_SKY; P3-1 of
   docs/port/NATIVE_DRAW_TASKS.md, NATIVE_DRAW.md 1.2.3 and A1).

   The sky is not a pass of its own: it is the map record with flag bit 31 (MapDrawRecord.flags < 0), on
   maps 000, 002, 004, 013, 021, 027, 098 and 099, one per map. func_00112BD0 puts its model at the
   player's feet each frame, and Map_DrawRecords (or Map_DrawLateRecords) draws it in file order with the
   other records: Z GEQUAL, Z written, fog on (its PS2 fog kept by DrawDistance_SkyFog). So natively it is
   drawn exactly as a map record (NativeDraw_Posed, identity RD_KIND_MAP_RECORD and the record's address,
   which is what pictures between pair it by); only the switch is its own, so the sky can go back to the
   PS2 path while the rest of the map stays native, or the other way round.

   --sky ps2 is this: the dome at the feet with its real Z (A1's "PS2" column). --sky modern (centred on the
   eye, depth at the far plane, drawn first: P3-2) is the owner's decision first, because map geometry lies
   behind the dome on 002, 004, 013 and 027 (P0-3) and would show; when it comes, it goes here. */
#include "common.h"
#include "model/model.h"
#include "map/map.h"
#include "port/render.h"

int NativeDraw_Posed(ModelInstance *model, int kind, const void *owner); /* (native_draw.c) */

/* the --native-draw kind a map record belongs to: the sky (flag bit 31) or the map */
static int record_kind(const MapDrawRecord *record) {
    return record->flags < 0 ? PORT_ND_SKY : PORT_ND_MAP;
}

/* The sky record, natively. --sky ps2: as every map record (its model is already at the feet, its fog
   block already the PS2's) */
static int sky_record(ModelInstance *model, const MapDrawRecord *record) {
    return NativeDraw_Posed(model, RD_KIND_MAP_RECORD, record);
}

/* Map_DrawRecords' and Map_DrawLateRecords' call site (behind port_native_draw(PORT_ND_MAP | PORT_ND_SKY)):
   the record's draw_parts, natively if its kind (record_kind) is on. 1 if drawn; 0 if its kind is off or
   it cannot be drawn natively (NativeDraw_Posed), and nothing was queued: the caller draws it the PS2 way. */
int NativeDraw_MapRecord(ModelInstance *model, const MapDrawRecord *record) {
    int kind = record_kind(record);

    if (!port_native_draw(kind)) {
        return 0;
    }
    if (kind == PORT_ND_SKY) {
        return sky_record(model, record);
    }
    return NativeDraw_Posed(model, RD_KIND_MAP_RECORD, record);
}
