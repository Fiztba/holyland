#include "kf_map.h"
#include "kf_player.h"
#include "kf_screen.h"
#include "kf_game.h"
#include "kf_splash.h"
#include "kf_gs.h"
#include "kf_packet.h"
#include "kf_entity.h"
#include "kf_entity_world.h"
#include "kf_gold.h"
#include "kf_linked_point.h"
#include "kf_map_object.h"
#include "kf_effect.h"
#include "kf_file.h"
#include "kf_map_scroll.h"
#include "kf_sound.h"

extern void (*D_0038D650[])(void);
extern void (*D_0038D670[])(void);
int func_00103A18(int, int *, int *);
void func_00101618(void);
void func_001020F8(void);
void func_001095E0(void);
void func_001096E8(void);
void func_00109800(void);
void func_0010B000(void);
void func_00118A50(void);
void func_001B5460(void);
void Display_SwapBuffers(int);
int func_001005E8(int);
void func_0010A040(int);
void func_00109A50(void);
void func_00109A80(int, s64);
void func_00109AD0(void);
void func_0010A100(void *);
void func_0010A4E8(int, int, int, int, int);
void func_00114198(int);
int func_00102230(void);
void func_00117CC8(void);
int func_00123108(void);
void func_00123288(void);
void Sound_Update(void);
void func_00115878(int);
void func_00117C60(void);
void func_001A15F8(void);
void func_001BC128(void);
void func_001C8938(int);
void func_00102218(void);
void func_00102228(void);
int func_00102900(int);
int sprintf(char *, char *, ...);
float cosf(float);
float sinf(float);
void func_001034F8(int);
void func_00106178(int, int, int, int);
void func_00114840(int);
void func_00114AA8(int);
void func_001155F8(int);
void func_00119360(int);
void func_00123158(int);
void Sound_StopAll(int);
void func_001A15F0(void);
void func_001C82C0(int);
void func_001C87D8(int);
void Script_Init(int);
int Script_Start(int, int);
void Vector_Copy(void *, void *);
extern int D_00446770;
extern MapSlot D_0038CD20[2];
extern ScreenFade D_0038D680;
extern PlayerState D_00413F80;
extern GameStatus D_0038CC50;
extern MapLoadState D_0038D610;
extern MapProp *D_0038CDE0[2][0xFF];
extern MapDrawRecord *D_00446778[2];
extern MapDrawRecord *D_00446780[2];
extern AreaNode *D_00446788;
extern AreaNode *D_0044678C;
extern MapSlotMemory D_004321F0[2];
extern char D_0043E720[];
extern u32 D_00446F1C;
extern char *D_00446F50;
extern char *D_00446F54;
extern SoundLoadQueue D_0038A130;

/* Start a transition to new_value unless one is running or it is already current. */
void func_001187F0(int new_value) {
    MapSlot *slot;

    slot = &D_0038CD20[D_00446770];
    if (D_0038D680.step == FALSE && slot->tag != new_value) {
        D_0038D680.step = TRUE;
        D_0038D680.timer = 0x1E;
        D_0038D680.map = new_value;
        D_0038D680.draw_step = 0;
        D_0038D680.kind = 0;
    }
}

/* Moves the player to position in the current map, or starts the change to another map. */
void func_00118848(int map, Vector4 *position) {
    MapSlot *slot = &D_0038CD20[D_00446770];

    if (D_0038D680.step == 0) {
        if (slot->tag == map) {
            if (position != NULL) {
                D_00413F80.position.x = position->x;
                D_00413F80.position.y = position->y;
                D_00413F80.position.z = position->z;
            }
        } else {
            D_0038D680.map = map;
            D_0038D680.position.x = position->x;
            D_0038D680.position.y = position->y;
            D_0038D680.position.z = position->z;
            D_0038D680.timer = 30;
            D_0038D680.step = 1;
            D_0038D680.kind = 1;
            D_0038D680.draw_step = 0;
            D_0038D680.position.w = 0.0f;
        }
    }
}

/* Starts the map change that entry (5 and up) of the table func_00103A18 reads leads to. */
int func_001188E0(int entry) {
    int target[2];
    int map;

    if (D_0038D680.step != 0) {
        return FALSE;
    }
    if (func_00103A18(entry - 5, &target[0], &target[1]) == 0) {
        return FALSE;
    }
    map = target[0];
    D_0038D680.step = 1;
    D_0038D680.unk14 = target[1];
    D_0038D680.timer = 30;
    D_0038D680.map = map;
    D_0038D680.kind = 2;
    D_0038D680.draw_step = 0;
    return TRUE;
}

/* Runs a pending map change to the end, drawing its own frames meanwhile. */
void func_00118958(void) {
    if (D_0038D680.step != 0) {
        func_00118A50();
        func_001020F8();
        Splash_Abort();
        do {
            func_001B5460();
            Display_SwapBuffers(0);
            func_001096E8();
            func_001095E0();
            func_0010B000();
            D_0038D670[D_0038D680.draw_step]();
            D_0038D650[D_0038D680.step]();
            func_00101618();
            ((u8 (*)(void))func_00109800)(); // match: called as returning a value, so the loop test uses $3
        } while (D_0038D680.step != 0);
        D_0038CC50.bit2 = FALSE;
        D_0038CC50.save_requested = FALSE;
    }
}

/* Draws one frame with nothing in it. */
void func_00118A50(void) {
    Display_SwapBuffers(0);
    func_001096E8();
    func_001095E0();
    func_0010B000();
    func_001005E8(0);
    func_0010A040(0x2BC0);
    func_00109800();
}

/* Map change drawing step: darkens the screen in ten strips, more each frame, until the timer runs out. */
void func_00118A98(void) {
    ScreenQuad quad;
    int remaining = 9;
    int total;

    func_00109A50();
    func_00109A80(GS_REG_TEST_1, 0x30000);
    func_00109A80(GS_REG_ZBUF_1, 0x10A000118L);
    func_00109A80(GS_REG_ALPHA_1, 0x88);
    func_00109AD0();
    total = 30;
    quad.tag = 0x26802ABC0L;
    quad.unk10 = 0x156;
    quad.first_x = 0x6BF8;
    quad.first_y = 0x71F8;
    quad.second_x = 0x7000;
    quad.second_y = 0x8E00;
    quad.unk08 = 0;
    quad.second_z = quad.first_z = 0xFFFFFFFF;
    quad.second_u = 0x400;
    quad.second_v = 0x1C00;
    quad.first_u = 0;
    quad.first_v = 0;
    quad.red = quad.green = quad.blue = (D_0038D680.timer << 7) / total;
    quad.alpha = 0x80;
    do {
        remaining--;
        func_0010A100(&quad);
        quad.first_x += 0x400;
        quad.second_x += 0x400;
        quad.first_u += 0x400;
        quad.second_u += 0x400;
    } while (remaining >= 0);
    func_00109A50();
    func_00109A80(GS_REG_ZBUF_1, 0xA000118);
    func_00109AD0();
    if (--D_0038D680.timer <= 0) {
        D_0038D680.draw_step++;
    }
}

/* Map change drawing step: keeps the screen black. */
void func_00118C28(void) {
    func_00109A50();
    func_00109A80(GS_REG_TEST_1, 0x30000);
    func_00109A80(GS_REG_ZBUF_1, 0x10A000118L);
    func_00109AD0();
    func_0010A4E8(0, 0, 0, 0x80, 0x88);
    func_00109A50();
    func_00109A80(GS_REG_ZBUF_1, 0xA000118);
    func_00109AD0();
}

/* Draws one frame of a 30-frame fade and counts the timer down. */
void func_00118CA8(void) {
    func_00114198(0);
    func_00109A50();
    func_00109A80(GS_REG_TEST_1, 0x30000);
    func_00109A80(GS_REG_ZBUF_1, 0x10A000118L);
    func_00109A80(GS_REG_ALPHA_1, 0x88);
    func_00109AD0();
    {
        int total = 30; /* two separate 30s, as retail keeps both */
        int span = 30;
        func_0010A4E8(0, 0, 0, ((total - D_0038D680.timer) << 7) / span, 0x88);
    }
    func_00109A50();
    func_00109A80(GS_REG_ZBUF_1, 0xA000118);
    func_00109AD0();
    D_0038D680.timer--;
}

void func_00118D78(void) {
}

/* Map change step: lets the background load and everything else in flight settle. */
void func_00118D80(void) {
    func_00117CC8();
    func_00123288();
    Sound_Update();
    if (D_0038D610.step == 0 || D_0038D610.step == 5) {
        if (func_00123108() == 0 && D_0038A130.unk0C == 0 && D_0038A130.unk04 == 0 && D_0038A130.unk18 == 0 &&
            func_00102230() == 0) {
            D_0038D680.step++;
        }
    }
}

/* Map change step: frees everything both map slots hold and clears the map tables. */
void func_00118E20(void) {
    int slot;

    for (slot = 0; slot < 2; slot++) {
        if (D_0038CD20[slot].unk00 != 0) {
            func_00115878(slot);
            Effect_FreeForMap(slot);
            MapObject_UnloadMap(slot);
            Entity_FreeAllInGroup(slot);
            Entity_ClearMapSlot(slot);
            Gold_UnlinkMap(slot);
            func_001C8938(slot);
            LinkedPoint_ReleaseByKind(slot);
        }
    }
    func_00117C60();
    func_001A15F8();
    Entity_ResetAllLists();
    func_001BC128();
    Effect_FreePlayerMagic();
    D_00446770 = 0;
    memset(D_0038CD20, 0, sizeof(D_0038CD20));
    memset(D_00446778, 0, sizeof(D_00446778));
    memset(D_00446780, 0, sizeof(D_00446780));
    memset(D_0038CDE0, 0, sizeof(D_0038CDE0));
    D_00446788 = NULL;
    D_0044678C = NULL;
    D_0038D680.step++;
}

/* Map change step: opens the new map file and starts reading all of it into the memory of slot 0. */
void func_00118F40(void) {
    char name[0x100];
    MapSlot *slot = &D_0038CD20[0];
    MapSlotMemory *memory = &D_004321F0[0];
    int size;

    sprintf(name, D_0043E720, D_0038D680.map);
    D_0038D680.file_handle = func_00102238(name, 1, 1);
    if (D_0038D680.file_handle < 0) {
        for (;;) {
        }
    }
    func_00102218();
    size = (func_00102900(D_0038D680.file_handle) + 0x3FFFF) / 0x40000 * 0x40000;
    memory->unk14 = memory->unk1C = D_00446F50;
    memory->unk18 = memory->unk20 = D_00446F54;
    slot->unk04 = size;
    memset(memory->unk1C, 0, slot->unk04);
    memset(memory->unk20, 0, D_00446F1C / 2);
    func_001025C0(D_0038D680.file_handle, memory->unk14, -1);
    D_0038D680.step++;
}

/* Map change step: once the whole file is read, closes it and goes on. */
void func_00119068(void) {
    int next_step;

    if (func_00102790(D_0038D680.file_handle, 1) == 0) {
        func_001024B0(D_0038D680.file_handle);
        func_00102228();
        next_step = D_0038D680.step + 1;
        D_0038D680.file_handle = -1;
        D_0038D680.step = next_step;
    }
}

/* Map change step: sets the loaded map up in slot 0, puts the player in it and ends the change. */
void func_001190C8(void) {
    MapSlot *slot = &D_0038CD20[0];
    MapObject *object;
    MapSlotUnk08 *header;
    f32 sine;
    f32 cosine;

    slot->tag = D_0038D680.map;
    func_00114840(0);
    func_00114AA8(0);
    func_001155F8(0);
    MapScroll_ClearPairs(0);
    func_001A15F0();
    MapObject_InitMap(0);
    func_001C82C0(0);
    Gold_InitMap(0);
    switch (D_0038D680.kind) {
    case 1:
        Vector_Copy(&D_00413F80, &D_0038D680);
        break;
    case 2:
        object = MapObject_GetById(0, D_0038D680.unk14);
        if (object != NULL) {
            sine = sinf(-object->model->rotation.y);
            cosine = cosf(-object->model->rotation.y);
            Vector_Copy(&D_00413F80, &object->model->position);
            D_00413F80.position.x += cosine * 0.0f - sine * -250.0f;
            D_00413F80.position.z += sine * 0.0f + cosine * -250.0f;
            D_00413F80.rotation.y = object->model->rotation.y;
            break;
        }
    case 0:
        header = slot->header;
        D_00413F80.position.x = header->unk60;
        D_00413F80.position.y = header->unk64;
        D_00413F80.position.z = header->unk68;
        D_00413F80.rotation.y = header->unk6C;
        break;
    }
    D_00413F80.map_index = 0xFFFF;
    D_00413F80.unk250 = 0xFF;
    D_00413F80.unk208 = 0;
    D_00413F80.unk204 = 0;
    D_00413F80.unk20C = 0;
    D_0038CD20[0].unk00 = 1;
    MapObject_StartMap(0);
    Entity_InitFromMapSpawns(0);
    func_00123158(0);
    func_001C87D8(0);
    Entity_ClearFlag10InSlot(0);
    func_00119360(0);
    func_001034F8(D_0038CD20[0].tag);
    Script_Init(D_0038CD20[0].unk5C);
    Script_Start(0x7FFF, 0);
    D_0038A130.unk00 = ((MapSlotUnk08 *)D_0038CD20[0].header)->unk70;
    D_0038A130.unk04 = 1;
    Sound_StopAll(1);
    D_0038A130.unk14 = &D_0038CD20[0];
    func_00106178(0, 0, 0, 30);
    D_0038D680.step = 0;
}

/* Map change step: ends the change once the timer has run out. */
void func_00119340(void) {
    if (D_0038D680.timer <= 0) {
        D_0038D680.step = 0;
    }
}
