/* The weapon sets mod's port side (port/include/port/weapon_sets.h, docs/port/WEAPON_SETS.md): its switch and
   command line, the swap key's press, and set 2 kept per save, as the automap keeps its explored maps
   (port/platform/automap.c): weapon_sets/live.txt in the data folder is the playthrough's, and each save
   gets a copy (card_NN.txt for the memory card's save file NN, quick_<file>.txt for a quick save), brought
   back when that save is loaded. A save made without one (or without the mod) brings back an empty set 2;
   a new game starts with an empty one. The game's side is game/weapon_sets.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

#include "cJSON.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/weapon_sets.h"

PortWeaponSetsOptions port_weapon_sets;
PortWeaponSet port_weapon_set2;
PortWeaponSetsMenu port_weapon_sets_menu;
PortWeaponSetsSwap port_weapon_sets_swap;

static unsigned long long pressed_at;
static int started;
static int loaded_since_title = 1; /* a save was loaded since the title was reached */
static int at_title;

int port_weapon_sets_option(int argc, char **argv, int *i) {
    (void)argc;
    if (strcmp(argv[*i], "--weapon-sets") == 0) {
        port_weapon_sets.enabled = port_weapon_sets.forced = 1;
        return 1;
    }
    return 0;
}

/* ---- settings.json ------------------------------------------------------------------------------- */
void port_weapon_sets_settings_read(const void *mods_json) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive((const cJSON *)mods_json, "weapon_sets");

    if (!port_weapon_sets.forced && cJSON_IsBool(item)) {
        port_weapon_sets.enabled = cJSON_IsTrue(item);
    }
}

void port_weapon_sets_settings_write(void *mods_json) {
    cJSON *mods = mods_json;
    cJSON *value = cJSON_CreateBool(port_weapon_sets.enabled);

    if (cJSON_GetObjectItemCaseSensitive(mods, "weapon_sets") != NULL) {
        cJSON_ReplaceItemInObjectCaseSensitive(mods, "weapon_sets", value);
    } else {
        cJSON_AddItemToObject(mods, "weapon_sets", value);
    }
}

void port_weapon_sets_set_enabled(int on) {
    port_weapon_sets.enabled = on != 0;
    port_settings_changed();
}

/* ---- The key ------------------------------------------------------------------------------------- */
void port_weapon_sets_key(void) {
    if (port_weapon_sets.enabled) {
        pressed_at = port_vblank_count() + 1; /* never 0 */
    }
}

unsigned long long port_weapon_sets_pressed(void) {
    return pressed_at;
}

void port_weapon_sets_take_press(void) {
    pressed_at = 0;
}

/* ---- The files ----------------------------------------------------------------------------------- */
static void folder(char *out, size_t size) {
    snprintf(out, size, "%s/weapon_sets", port_data_dir());
#ifdef _WIN32
    _mkdir(port_data_dir());
    _mkdir(out);
#else
    mkdir(port_data_dir(), 0755);
    mkdir(out, 0755);
#endif
}

static int write_set(const char *name) {
    char dir[1024], path[1200], temp[1300];
    FILE *file;
    int ok;

    folder(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(temp, sizeof(temp), "%s.part", path);
    file = fopen(temp, "w");
    ok = file != NULL && fprintf(file, "set2 weapon %d shield %d\n", port_weapon_set2.weapon, port_weapon_set2.shield) > 0;
    if (file != NULL) {
        ok = fclose(file) == 0 && ok;
    }
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
        ok = rename(temp, path) == 0;
#endif
    }
    if (!ok) {
        remove(temp);
        fprintf(stderr, "[weapon sets] cannot write %s\n", path);
    }
    return ok;
}

/* set 2 from the file; 0 (and set 2 left alone) when it is not there or not one */
static int read_set(const char *name) {
    char dir[1024], path[1200];
    FILE *file;
    int weapon, shield, ok;

    snprintf(dir, sizeof(dir), "%s/weapon_sets", port_data_dir()); /* nothing made to read */
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    ok = fscanf(file, "set2 weapon %d shield %d", &weapon, &shield) == 2 && weapon >= 0 && weapon < 70 && shield >= 0 &&
         shield < 30;
    fclose(file);
    if (ok) {
        port_weapon_set2.weapon = weapon;
        port_weapon_set2.shield = shield;
    }
    return ok;
}

static void start(void) {
    if (started) {
        return;
    }
    started = 1;
    if (read_set("live.txt")) {
        fprintf(stderr, "[weapon sets] set 2 read from weapon_sets/live.txt: weapon %d shield %d\n", port_weapon_set2.weapon,
                port_weapon_set2.shield);
    }
}

void port_weapon_sets_changed(void) {
    started = 1; /* set 2 as it is now is the one kept, not live.txt's */
    if (port_weapon_sets.enabled) {
        write_set("live.txt");
    }
}

/* a save's copy, or an empty set 2 when the save has none */
static void bring_back(const char *name, const char *what) {
    start();
    loaded_since_title = 1;
    if (read_set(name)) {
        port_event("weapon sets: set 2 of %s brought back (weapon %d, shield %d)", what, port_weapon_set2.weapon,
                   port_weapon_set2.shield);
    } else {
        port_weapon_set2.weapon = port_weapon_set2.shield = 0;
        port_event("weapon sets: %s has no set 2, it is empty", what);
    }
    write_set("live.txt");
}

/* /BASLUS-20318S/BASLUS-20318G05: the game's save file 05 */
void port_weapon_sets_card_file(const char *name, int writing) {
    const char *g;
    char file[64], what[64];

    if (!port_weapon_sets.enabled || name == NULL || (g = strstr(name, "BASLUS-20318G")) == NULL) {
        return;
    }
    g += strlen("BASLUS-20318G");
    if (g[0] < '0' || g[0] > '9' || g[1] < '0' || g[1] > '9' || g[2] != 0) {
        return;
    }
    snprintf(file, sizeof(file), "card_%c%c.txt", g[0], g[1]);
    snprintf(what, sizeof(what), "memory card save %c%c", g[0], g[1]);
    if (writing) {
        start();
        if (write_set(file)) {
            port_event("weapon sets: set 2 kept with %s", what);
        }
    } else {
        bring_back(file, what);
    }
}

static void quick_name(const char *path, char *out, size_t size) {
    const char *base = path, *p;

    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    snprintf(out, size, "quick_%s.txt", base);
}

void port_weapon_sets_quick_saved(const char *path) {
    char file[300];

    if (!port_weapon_sets.enabled) {
        return;
    }
    start();
    quick_name(path, file, sizeof(file));
    write_set(file);
}

void port_weapon_sets_quick_loading(const char *path) {
    char file[300];

    if (!port_weapon_sets.enabled) {
        return;
    }
    quick_name(path, file, sizeof(file));
    bring_back(file, file);
}

/* (game/quicksave.c) the title was reached: is the next game a new one? */
void port_weapon_sets_title(void) {
    if (port_weapon_sets.enabled) {
        start();
    }
    if (!at_title) {
        at_title = 1;
        loaded_since_title = 0;
    }
}

int port_weapon_sets_new_game(void) {
    int fresh = at_title && !loaded_since_title;

    at_title = 0;
    loaded_since_title = 1;
    return fresh;
}
