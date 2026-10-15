#include "kf_map.h"
#include "kf_file.h"
#include "kf_player.h"
#include "kf_entity.h"
#include "kf_entity_world.h"
#include "kf_gold.h"
#include "kf_linked_point.h"
#include "kf_map_object.h"
#include "kf_effect.h"
#include "kf_map_scroll.h"

extern void (*D_0038D5E0[])(void);
void func_00101618(void);
int func_001005E8(int);
int func_00102230(void);
void func_00102228(void);
void func_001095E0(void);
void func_001096E8(void);
void func_00109800(void);
void func_0010B000(void);
void func_00117F98(void);
void func_00123288(void);
void Sound_Update(void);
void func_001B5778(void);
void Display_SwapBuffers(int);
void func_001152B8(int);
void func_00115878(int);
void func_001A15F8(void);
void func_001C8938(int);
void func_00102218(void);
int func_00102900(int);
int sprintf(char *, char *, ...);
void func_00114840(int);
void func_00114AA8(int);
void func_001154D8(int, f32 *);
void func_001155F8(int);
void func_001A15F0(void);
void func_001C82C0(int);
void func_001C8990(int, f32 *);
void func_001034F8(int);
void func_001187B0(void);
void func_00119360(int);
void func_00123158(int);
void func_001C87D8(int);
extern MapLoadState D_0038D610;
extern MapLoadRequest D_0038D640;
extern int D_00446794;
extern MapSlot D_0038CD20[];
extern PlayerState D_00413F80;
extern int D_00446770;
extern MapSlotMemory D_004321F0[2];
extern char D_0043E710[];
extern u32 D_00446F1C;
extern int D_00446F20;
extern char *D_00446F50;
extern char *D_00446F54;

/* Resets the background map load and the pending request. */
void func_00117C60(void) {
    memset(&D_0038D610, 0, sizeof(MapLoadState));
    D_0038D610.file_handle = -1;
    D_0038D610.step = 0;
    D_0038D610.request = NULL;
    D_00446794 = 0;
    D_0038D640.x = 0.0f;
    D_0038D640.y = 0.0f;
    D_0038D640.z = 0.0f;
    D_0038D640.map = 0xFFFF;
}

/* Starts, queues and steps the background load of the map next to where the player stands. */
void func_00117CC8(void) {
    MapSlot *current = &D_0038CD20[D_00446770];
    MapSlot *other = (D_00446770 == 0) ? &D_0038CD20[1] : &D_0038CD20[0];
    MapRecordInfo *record;
    MapLoadRequest *request;

    if (D_00446794 != 0) {
        func_00117F98();
        return;
    }
    if (D_00413F80.map_index != 0xFFFF) {
        record = (MapRecordInfo *)current->records + D_00413F80.map_index;
    } else {
        record = NULL;
    }
    if (record != NULL && record->unk3A != 0xFF) {
        request = (MapLoadRequest *)&current->origins[record->unk3A];
        if (current->tag != request->map) {
            if (other->unk00 == 0 || other->tag != request->map) {
                if (D_0038D610.request == NULL || D_0038D610.request->map != request->map) {
                    if (D_0038D610.step == 0) {
                        D_0038D610.request = request;
                    } else {
                        D_0038D610.next_request = request;
                    }
                }
            }
        }
    }
    if (D_0038D610.next_request != NULL && D_0038D610.step != 4) {
        D_0038D610.step = 2;
        D_0038D610.request = D_0038D610.next_request;
        D_0038D610.next_request = NULL;
        if (D_0038D610.file_handle != -1) {
            func_001024B0(D_0038D610.file_handle);
            if (func_00102230() != 0) {
                func_00102228();
            }
            D_0038D610.file_handle = -1;
        }
    }
    if ((D_0038D610.flags.raw & 6) == 4) {
        if (D_0038D610.step == 0 || D_0038D610.step == 3 || D_0038D610.step == 5) {
            D_0038D610.flags.f.bit1 = TRUE;
            D_0038D610.flags.f.bit2 = FALSE;
        }
    }
    if (!((s32)(D_0038D610.flags.raw >> 1) & 1)) {
        if (record != NULL && record->unk39 != 0) {
            if ((u32)(D_0038D610.step - 1) < 5) {
                func_001B5778();
                while (D_0038D610.step != 5) {
                    func_001005E8(0);
                    func_00109800();
                    Display_SwapBuffers(0);
                    func_001096E8();
                    func_001095E0();
                    func_0010B000();
                    D_0038D5E0[D_0038D610.step]();
                    func_00123288();
                    func_00101618();
                    Sound_Update();
                }
                D_0038D610.step = 6;
            }
        }
        D_0038D5E0[D_0038D610.step]();
    }
}

/* Finishes the running map load at once, makes the loaded map current, puts the player at its start
   and frees the old map. */
void func_00117F98(void) {
    while (D_0038D610.step != 5) {
        func_001005E8(0);
        func_00109800();
        Display_SwapBuffers(0);
        func_001096E8();
        func_001095E0();
        func_0010B000();
        D_0038D5E0[D_0038D610.step]();
    }
    D_0038D610.step = 6;
    do {
        D_0038D5E0[D_0038D610.step]();
    } while (D_0038D610.step != 0);
    func_001152B8(D_00446770 == 0);
    D_00413F80.position.x = ((MapSlotUnk08 *)D_0038CD20[D_00446770].header)->unk60;
    D_00413F80.position.y = ((MapSlotUnk08 *)D_0038CD20[D_00446770].header)->unk64;
    D_00413F80.position.z = ((MapSlotUnk08 *)D_0038CD20[D_00446770].header)->unk68;
    D_00413F80.rotation.y = ((MapSlotUnk08 *)D_0038CD20[D_00446770].header)->unk6C;
    D_0038D640.map = 0xFFFF;
    D_00446794 = 0;
    D_00413F80.unk1AC = D_00413F80.unk1B0 = D_00413F80.unk1B4 = 0.0f;
    D_00413F80.unk1B8 = D_00413F80.unk1BC = 0.0f;
    D_0038D640.x = D_0038D640.z = D_0038D640.y = 0.0f;
    PLAYER_STATUS_BITS(&D_00413F80)->bit34 = FALSE;
    D_0038CD20[D_00446770 == 0].unk00 = 0;
    func_00115878(D_00446770 == 0);
    Effect_FreeForMap(D_00446770 == 0);
    MapObject_UnloadMap(D_00446770 == 0);
    Entity_FreeAllInGroup(D_00446770 == 0);
    Entity_ClearMapSlot(D_00446770 == 0);
    Gold_UnlinkMap(D_00446770 == 0);
    func_001C8938(D_00446770 == 0);
    LinkedPoint_ReleaseByKind(D_00446770 == 0);
    func_001A15F8();
}

/* Runs the pending per-slot refresh for the slot opposite the current one, then steps the counter. */
void func_001181B8(void) {
    int other_map;
    MapSlot *other;

    other_map = D_00446770 == 0;
    other = D_00446770 == 0 ? &D_0038CD20[1] : &D_0038CD20[0];
    if (D_0038D610.request != NULL) {
        if (other->unk00 != 0) {
            other->unk00 = 0;
            func_00115878(other_map);
            Effect_FreeForMap(other_map);
            MapObject_UnloadMap(other_map);
            Entity_FreeAllInGroup(other_map);
            Entity_ClearMapSlot(other_map);
            Gold_UnlinkMap(other_map);
            func_001C8938(other_map);
            LinkedPoint_ReleaseByKind(other_map);
            func_001A15F8();
        }
        D_0038D610.timer = 2;
        D_0038D610.step++;
    }
}

/* Map load step: waits for the timer to run out. */
void func_00118270(void) {
    if (--D_0038D610.timer <= 0) {
        D_0038D610.step++;
    }
}

/* Map load step: once the drive is free, opens the requested map file and picks the memory it loads into. */
void func_001182A0(void) {
    char name[0x100];
    MapSlotMemory *memory = (D_00446770 == 0) ? &D_004321F0[1] : &D_004321F0[0];
    MapSlot *other = (D_00446770 == 0) ? &D_0038CD20[1] : &D_0038CD20[0];
    MapSlot *current = &D_0038CD20[D_00446770];
    u32 limit;

    if (func_00102230() == 0) {
        sprintf(name, D_0043E710, D_0038D610.request->map);
        do {
            D_0038D610.file_handle = func_00102238(name, 1, 1);
        } while (D_0038D610.file_handle < 0);
        func_00102218();
        other->unk04 = (func_00102900(D_0038D610.file_handle) + 0x3FFFF) / 0x40000 * 0x40000;
        limit = D_00446F20;
        if (limit < (u32)(other->unk04 + current->unk04)) {
            other->tag = D_0038D610.request->map;
            for (;;) {
            }
        }
        if (D_00446770 != 0) {
            memory->unk1C = D_00446F50;
            memory->unk20 = D_00446F54;
            memory->unk14 = D_00446F50;
            memory->unk18 = D_00446F54;
        } else {
            memory->unk14 = memory->unk1C = D_00446F50 + limit - other->unk04;
            memory->unk18 = memory->unk20 = D_00446F54 + D_00446F1C / 2;
        }
        memset(memory->unk1C, 0, other->unk04);
        memset(memory->unk20, 0, D_00446F1C / 2);
        D_0038D610.step++;
    }
}

/* Map load step: reads the next 0x80000 bytes of the map file, closing it at the end. */
void func_00118458(void) {
    MapSlotMemory *memory = &D_004321F0[0];

    if (D_00446770 == 0) {
        memory = &D_004321F0[1];
    }
    if (func_001025C0(D_0038D610.file_handle, memory->unk14, 0x80000) > 0) {
        memory->unk14 += 0x80000;
        D_0038D610.step++;
    } else {
        func_001024B0(D_0038D610.file_handle);
        func_00102228();
        D_0038D610.file_handle = -1;
        memory->unk14 = memory->unk1C;
        D_0038D610.step += 2;
    }
}

/* Map load step: once the read in flight is done, goes back to read the next piece. */
void func_001184F8(void) {
    if (func_00102790(D_0038D610.file_handle, 1) == 0) {
        D_0038D610.step--;
    }
}

void func_00118538(void) {
}

/* Map load step: sets up the loaded map in the other slot, shifted so that it joins the current one. */
void func_00118540(void) {
    int other_slot = D_00446770 == 0;
    f32 offset[4];

    offset[0] = -D_0038D610.request->x;
    offset[1] = -D_0038D610.request->y;
    offset[2] = -D_0038D610.request->z;
    offset[3] = 0.0f;
    if (D_00446794 == 0) {
        D_0038CD20[other_slot].tag = D_0038D610.request->map;
    } else {
        D_0038CD20[other_slot].tag = D_0038D640.map;
    }
    func_00114840(other_slot);
    func_00114AA8(other_slot);
    func_001154D8(other_slot, offset);
    func_001155F8(other_slot);
    MapScroll_ClearPairs(other_slot);
    func_001A15F0();
    MapObject_InitMap(other_slot);
    MapObject_OffsetAll(other_slot, offset);
    func_001C82C0(other_slot);
    func_001C8990(other_slot, offset);
    Gold_InitMap(other_slot);
    Gold_MoveMap(other_slot, offset);
    D_0038D610.step++;
}

/* Map load step: nothing to wait for, goes on. */
void func_00118668(void) {
    D_0038D610.step++;
}

/* Map load step: brings the other slot to life and ends the load. */
void func_00118680(void) {
    MapSlot *slot = &D_0038CD20[0];
    int other_slot;
    f32 offset[4];

    if (D_00446770 == 0) {
        slot = &D_0038CD20[1];
    }
    other_slot = D_00446770 == 0;
    offset[0] = -D_0038D610.request->x;
    offset[1] = -D_0038D610.request->y;
    offset[2] = -D_0038D610.request->z;
    offset[3] = 0.0f;
    slot->unk00 = 1;
    MapObject_StartMap(other_slot);
    Entity_InitFromMapSpawns(other_slot);
    Entity_OffsetAllInSlot(other_slot, offset);
    func_00123158(other_slot);
    func_001C87D8(other_slot);
    Entity_ClearFlag10InSlot(other_slot);
    MapScroll_CarryPairs(D_00446770, other_slot);
    func_00119360(other_slot);
    func_001034F8(slot->tag);
    D_0038D610.position.x = D_0038D610.request->x;
    D_0038D610.position.y = D_0038D610.request->y;
    D_0038D610.position.z = D_0038D610.request->z;
    D_0038D610.position.w = 0.0f;
    D_0038D610.step = 0;
    D_0038D610.request = NULL;
    func_001187B0();
}

void func_00118788(void) {
    if (!(D_0038D610.flags.raw & 6)) {
        D_0038D610.flags.raw |= 4;
    }
}

void func_001187B0(void) {
    D_0038D610.flags.f.bit1 = FALSE;
    D_0038D610.flags.f.bit2 = FALSE;
}

/* Whether the background load is held off. */
int func_001187D8(void) {
    // match: bit 1 of the flags taken out with shifts; the bit-field reads it another way
    return (int)(((s64)D_0038D610.flags.raw << 31) >> 32) & 1;
}
