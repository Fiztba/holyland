/* game_dump.c: F10's snapshot of the game for a bug report (port/debug/debug.c writes the file): the
   map, the player, the game's status, the live entities, the map objects near the player and the
   flags the story keeps. Compiled with the game code (it reads the decomp's structures), and only
   reads them. Runs on the game's thread at a vblank, where every EE thread is parked. */
#include <math.h>
#include <stdio.h>

#include "common.h"
#include "core/game.h"
#include "entity/entity.h"
#include "map/map.h"
#include "map_object/map_object.h"
#include "model/model.h"
#include "player/player.h"
#include "port/debug.h"

extern GameStatus game_status;
extern PlayerState player_state;
extern MapSlot map_slots[2];
extern int current_map_slot;
extern u32 frame_counter;
extern s8 main_menu_state;
extern int save_loaded;
extern MapObject map_slot_objects[2][0x200];
extern u8 map_script_flags[0x2000];
extern u8 story_flags[0x40];
extern u8 map_visited_flags[0x10];
extern RecollectionFlags recollection_flags[50];
void kf4_game_state(char *line, size_t size); /* game/state_log.c */

#define NEAR_OBJECTS 6000.0f /* map objects within this distance of the player */

static void json_floats(FILE *f, const char *name, const float *v, int n) {
    int i;

    fprintf(f, "\"%s\": [", name);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%.3f", i ? ", " : "", v[i]);
    }
    fprintf(f, "]");
}

static void json_s16s(FILE *f, const char *name, const s16 *v, int n) {
    int i;

    fprintf(f, "\"%s\": [", name);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%d", i ? ", " : "", v[i]);
    }
    fprintf(f, "]");
}

static void json_u16s(FILE *f, const char *name, const u16 *v, int n) {
    int i;

    fprintf(f, "\"%s\": [", name);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%u", i ? ", " : "", v[i]);
    }
    fprintf(f, "]");
}

/* the bytes of a table that are not 0, as "index": value */
static void json_nonzero(FILE *f, const char *name, const u8 *bytes, int n, int row) {
    int i, first = 1;

    fprintf(f, "  \"%s\": {", name);
    for (i = 0; i < n; i++) {
        if (bytes[i] != 0) {
            if (row) {
                fprintf(f, "%s\"%02X:%02X\": %u", first ? "" : ", ", i / row, i % row, bytes[i]);
            } else {
                fprintf(f, "%s\"%02X\": %u", first ? "" : ", ", i, bytes[i]);
            }
            first = 0;
        }
    }
    fprintf(f, "},\n");
}

void kf4_game_dump(FILE *f) {
    const PlayerState *p = &player_state;
    const MapSlot *slot = &map_slots[current_map_slot & 1];
    const Entity *e;
    int i, s, count;

    fprintf(f, "  \"frame\": %u, \"menu\": %d, \"save_loaded\": %d,\n", (unsigned)frame_counter, main_menu_state,
            save_loaded);
    fprintf(f, "  \"status\": \"%08X\", \"player_control\": %d, \"paused\": %d, \"camera_mode\": %d,\n",
            (unsigned)GAME_STATUS_FLAG_WORD(&game_status), (int)game_status.player_control, (int)game_status.paused,
            (int)game_status.camera_mode);
    fprintf(f, "  \"map\": {\"slot\": %d, \"tag\": \"%02X\", \"loaded\": %d, \"other_slot_tag\": \"%02X\", "
               "\"other_slot_loaded\": %d, \"record\": %u},\n",
            current_map_slot, slot->tag, slot->loaded, map_slots[(current_map_slot & 1) ^ 1].tag,
            map_slots[(current_map_slot & 1) ^ 1].loaded, (unsigned)p->map_index);

    fprintf(f, "  \"player\": {");
    json_floats(f, "position", &p->position.x, 3);
    fprintf(f, ", ");
    json_floats(f, "rotation", &p->rotation.x, 3);
    fprintf(f, ",\n    \"hp\": %d, \"hp_max\": %d, \"mp\": %d, \"mp_max\": %d, \"level\": %d, \"experience\": %d, "
               "\"gold\": %d,\n",
            p->hp_now, p->hp_max, p->mp_now, p->mp_max, p->level, p->experience, p->gold);
    fprintf(f, "    \"power\": %d, \"power_base\": %d, \"magic\": %d, \"magic_base\": %d, \"power_gauge\": %d, "
               "\"magic_gauge\": %d, \"weight\": %d, \"max_weight\": %d,\n",
            p->power, p->power_base, p->magic, p->magic_base, p->power_gauge, p->magic_gauge, p->weight,
            p->max_weight);
    fprintf(f, "    \"weapon\": %u, \"equipped_magic\": %u, \"equipment\": [", p->weapon, p->equipped_magic);
    for (i = 0; i < 8; i++) {
        fprintf(f, "%s%u", i ? ", " : "", p->equipment[i]);
    }
    fprintf(f, "],\n    ");
    json_s16s(f, "attack_stats", p->attack_stats.values, 9);
    fprintf(f, ", ");
    json_s16s(f, "defence_stats", p->defence_stats.values, 9);
    fprintf(f, ",\n    ");
    json_s16s(f, "timed_flags", p->timed_flags, 6);
    fprintf(f, ", ");
    json_u16s(f, "flag_timers", p->flag_timers, 6);
    fprintf(f, ", ");
    json_u16s(f, "ailment_defence", p->ailment_defence, 6);
    fprintf(f, ",\n    \"status_bits\": \"%016llX\", \"ground_effect_kind\": %d, \"death_timer\": %u, "
               "\"light_spell_timer\": %d, \"rage_timer\": %d},\n",
            (unsigned long long)p->status_bits, p->ground_effect_kind, p->death_timer, p->light_spell_timer,
            p->rage_timer);

    /* the live entities, in the game's own list */
    fprintf(f, "  \"entities\": [");
    count = 0;
    for (e = active_entities_head; e != NULL && count < 256; e = e->next, count++) {
        const float *pos = e->model != NULL ? e->model->position : NULL;
        const float *rot = e->model != NULL ? e->model->rotation : NULL;

        fprintf(f, "%s\n    {\"type\": %u, \"spawn\": %u, \"mode\": %u, \"hp\": %u, \"state\": %d, "
                   "\"action_step\": %d, \"busy\": \"%04X\", \"flags\": \"%016llX\", \"group\": %d, "
                   "\"handler\": \"%p\", \"distance\": %.1f, ",
                count ? "," : "", e->spawn_info != NULL ? e->spawn_info->type_id : 0xFFFFu, e->spawn_index,
                e->spawn_info != NULL ? e->spawn_info->spawn_mode : 0u, e->hit_points, e->state, e->action_step,
                e->busy_flags, (unsigned long long)e->flags, e->group_id, (void *)e->handler,
                e->distance_to_player);
        if (pos != NULL) {
            json_floats(f, "position", pos, 3);
            fprintf(f, ", \"heading\": %.3f", rot[1]);
        } else {
            fprintf(f, "\"position\": null");
        }
        fprintf(f, ", \"ailments\": [%u, %u, %u, %u]}", e->ailment_1_frames, e->ailment_2_frames,
                e->ailment_3_frames, e->ailment_4_frames);
    }
    fprintf(f, "\n  ],\n");

    /* the map objects of both slots near the player */
    fprintf(f, "  \"map_objects\": [");
    count = 0;
    for (s = 0; s < 2; s++) {
        for (i = 0; i < map_object_counts[s] && i < 0x200; i++) {
            const MapObject *o = &map_slot_objects[s][i];
            float dx = o->last_position_x - p->position.x, dy = o->last_position_y - p->position.y;
            float dz = o->last_position_z - p->position.z;
            float distance = sqrtf(dx * dx + dy * dy + dz * dz);

            if (o->definition == NULL || distance > NEAR_OBJECTS) {
                continue;
            }
            fprintf(f, "%s\n    {\"slot\": %d, \"index\": %d, \"kind\": %u, \"def\": %u, \"state\": \"%08X\", "
                       "\"distance\": %.1f, \"position\": [%.1f, %.1f, %.1f], \"rotation_y\": %.3f, "
                       "\"use_script\": %u, \"work\": [%d, %d, %d, %d, %d, %d, %d, %d]}",
                    count++ ? "," : "", s, i, MAP_OBJECT_DEF_KIND(o->definition),
                    o->source != NULL ? o->source->def_index : 0xFFFFu, (unsigned)o->state_flags, distance,
                    o->last_position_x, o->last_position_y, o->last_position_z, o->last_rotation_y,
                    o->source != NULL ? o->source->use_script : 0u, (int)o->work[0], (int)o->work[1],
                    (int)o->work[2], (int)o->work[3], (int)o->work[4], (int)o->work[5], (int)o->work[6],
                    (int)o->work[7]);
        }
    }
    fprintf(f, "\n  ],\n");

    /* the flags the story keeps (only those set) */
    json_nonzero(f, "story_flags", story_flags, 0x40, 0);
    json_nonzero(f, "map_visited_flags", map_visited_flags, 0x10, 0);
    json_nonzero(f, "map_script_flags", map_script_flags, 0x2000, 0x40);
    json_nonzero(f, "this_map_script_flags", map_script_flags + (slot->tag & 0x7F) * 0x40, 0x40, 0);
    json_nonzero(f, "recollection_flags", (const u8 *)recollection_flags, (int)sizeof(recollection_flags), 8);
    {
        char line[512];

        kf4_game_state(line, sizeof(line));
        fprintf(f, "  \"state_line\": \"%s\"\n", line);
    }
}
