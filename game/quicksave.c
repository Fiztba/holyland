/* quicksave.c: PC-only. The game's side of quick saves (port/include/port/quicksave.h).

   Save: Game_WriteSaveImage (core/game.c) writes the game into save_image as the save screen does
   before a memory card save (ui/trade.c, after PlayTime_Update); the image goes to a file instead of
   the card. save_image is put back as it was afterwards.

   Load: the image goes into save_image as a memory card load leaves it (save/save_card.c,
   SaveCard_StepLoad: save_loaded = 1, game_status.bit9 = 1). In play that ends the main loop
   (GameLoop_Run), and main (main.c) then runs GameLoop_ResetSystems, which reads the image back
   (Game_ReadSaveImage), and GameLoop_Run on the saved map (saved_map_tag), which leaves the player
   where the image says: the image holds the position and the rotation (PlayerState 0x00 to 0x20). On
   the title screen, Title_Run returns as the title's own Load does when a save was loaded.

   The patched game calls Quick_PollGame once a frame at the end of the main loop, and Quick_PollTitle
   once a frame on the title screens; both return at once when nothing is asked for. */
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "core/game.h"
#include "map/map.h"
#include "player/player.h"
#include "gfx/screen.h"
#include "ui/message.h"
#include "port/quicksave.h"

#define SAVE_IMAGE_SIZE 0x20000
#define SAVE_IMAGE_MAGIC 0x15400

extern u8 save_image[SAVE_IMAGE_SIZE];
extern int save_loaded;
extern GameStatus game_status;
extern PlayerState player_state;
extern MapSlot map_slots[2];
extern int current_map_slot;
extern ScreenFade screen_fade;
extern int map_load_committed;
extern sptr D_00447000[4];
extern int fade_frames_left;
extern ClockTime current_play_time;

void Game_WriteSaveImage(void);
int Game_CheckSaveImage(void);
void PlayTime_Update(void);

/* a load was handed to the game: the first vblank of play checks where the game put the player */
static int placed_check;

static void fill_info(PortQuickInfo *info) {
    memset(info, 0, sizeof(*info));
    info->map_tag = map_slots[current_map_slot & 1].tag;
    info->position[0] = player_state.position.x;
    info->position[1] = player_state.position.y;
    info->position[2] = player_state.position.z;
    info->rotation[0] = player_state.rotation.x;
    info->rotation[1] = player_state.rotation.y;
    info->rotation[2] = player_state.rotation.z;
    info->hp = player_state.hp_now;
    info->hp_max = player_state.hp_max;
    info->mp = player_state.mp_now;
    info->mp_max = player_state.mp_max;
    info->level = player_state.level;
    info->gold = player_state.gold;
    /* as PlayTime_AsSeconds counts it: mday is the hundreds of hours, in 99s */
    info->play_seconds = current_play_time.sec + (current_play_time.min + (current_play_time.hour + current_play_time.mday * 99u) * 60u) * 60u;
}

/* Why the player is not in control now (NULL: he is) */
static const char *not_in_play(void) {
    if (game_status.bit9) {
        return "the game is leaving play";
    }
    if (player_state.death_timer != 0) {
        return "the player is dying";
    }
    if (!map_slots[current_map_slot & 1].loaded) {
        return "no map is loaded";
    }
    if (game_status.player_control == 0 || (game_status.update_hold_flags & 1)) {
        return "an event has the player's control";
    }
    if (game_status.paused || (game_status.bit12 && game_status.bit13)) {
        return "the game is paused";
    }
    if (Message_IsBoxOpen()) {
        return "a message is open";
    }
    if (screen_fade.step != 0 || map_load_committed != 0) {
        return "the map is changing";
    }
    if (D_00447000[3] != 0 && fade_frames_left > 0) {
        return "the screen is fading";
    }
    if (game_status.bit2 || game_status.bit3 || game_status.bit4 || game_status.save_requested || game_status.bit8) {
        return "a menu is opening";
    }
    return NULL;
}

static void quick_save(void) {
    PortQuickInfo info;
    u8 *kept = malloc(SAVE_IMAGE_SIZE);
    u32 size;
    const char *why = not_in_play();

    if (why != NULL || kept == NULL) {
        free(kept);
        port_quick_refuse(why != NULL ? why : "out of memory");
        return;
    }
    memcpy(kept, save_image, SAVE_IMAGE_SIZE);
    fill_info(&info);
    PlayTime_Update();
    Game_WriteSaveImage();
    fill_info(&info); /* the play time just read */
    size = *(u32 *)(save_image + 4);
    /* the blocks, then their byte sums (one per 128 bytes) */
    port_quick_write(save_image, size + size / 128, &info);
    memcpy(save_image, kept, SAVE_IMAGE_SIZE);
    free(kept);
}

/* Puts a quick save's image where a memory card load puts it: 1 when the game's check passes */
static int quick_load_image(void) {
    PortQuickInfo info;
    size_t size;
    u8 *image = port_quick_read(&size, &info);
    u8 *kept;

    if (image == NULL) {
        return 0;
    }
    kept = malloc(SAVE_IMAGE_SIZE);
    if (kept == NULL || size > SAVE_IMAGE_SIZE) {
        free(image);
        free(kept);
        port_quick_loaded(0);
        return 0;
    }
    memcpy(kept, save_image, SAVE_IMAGE_SIZE);
    memset(save_image, 0, SAVE_IMAGE_SIZE); /* as ui/menu_screens.c clears it before a card load */
    memcpy(save_image, image, size);
    free(image);
    if (*(u32 *)save_image != SAVE_IMAGE_MAGIC || !Game_CheckSaveImage()) {
        memcpy(save_image, kept, SAVE_IMAGE_SIZE);
        free(kept);
        port_quick_loaded(0);
        return 0;
    }
    free(kept);
    save_loaded = 1;
    placed_check = 1;
    port_quick_loaded(1);
    return 1;
}

/* (port/platform/quicksave.c, each vblank) GameLoop_Run clears save_loaded once it has loaded the map,
   and the first vblank after is the top of the main loop's first frame: the player stands where the
   game put him, before the frame moves him */
void kf4_quick_vblank(void) {
    if (placed_check && !save_loaded) {
        PortQuickInfo now;

        placed_check = 0;
        fill_info(&now);
        port_quick_placed(&now);
    }
}

void Quick_PollGame(void) {
    int wanted = port_quick_wanted();

    if (wanted == PORT_QUICK_SAVE) {
        quick_save();
    } else if (wanted == PORT_QUICK_LOAD) {
        if (game_status.bit9) {
            port_quick_refuse("the game is leaving play");
            return;
        }
        if (quick_load_image()) {
            /* leave the main loop as a card load does; main then restarts the session from the image.
               A dying player's game over and the ending are not shown */
            player_state.death_timer = 0;
            game_status.bits10 = 0;
            game_status.bit9 = 1;
        }
    }
}

int Quick_PollTitle(void) {
    int wanted = port_quick_wanted();

    if (wanted == PORT_QUICK_SAVE) {
        port_quick_refuse("no game is being played");
    } else if (wanted == PORT_QUICK_LOAD) {
        return quick_load_image();
    }
    return 0;
}
