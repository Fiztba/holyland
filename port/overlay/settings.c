/* settings.json (port/include/port/settings.h): the player's settings, in the user's data folder.

     {
       "display":  {"window_scale": 2, "fullscreen": false, "aspect": "4:3", "vsync": false, "smooth": true,
                    "deinterlace": true, "progressive": true, "anti_blur": true, "adapt_width": true},
       "audio":    {"volume": 100, "mute": false},
       "controls": {"layout": "ps2", "mouse_sensitivity": 1.0, "invert_y": false,
                    "menu_mouse": ["cross", "circle", "cross"],
                    "bindings": {"verdite": {"square": ["key:Space", "mouse:left", "pad:x"]}}},
       "overlay":  {"pause_when_open": false, "show_fps": false, "scale": 1.0, "save_slot": 0}
     }

   "bindings" holds only the actions that differ from their layout's defaults (port/sdk/controls.c),
   each as its three slots: two keys or mouse buttons, and a controller button. Keys this version does
   not know are kept as they are. A missing or broken file means the defaults. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

#include "cJSON.h"
#include "port/audio.h"
#include "port/controls.h"
#include "port/host.h"
#include "port/platform.h"
#include "port/settings.h"

PortSettings port_settings = {
    .window_scale = 2,
    .smooth = 1,
    .deinterlace = 1,
    .progressive = 1,
    .anti_blur = 1,
    .adapt_width = 1,
    .volume = 100,
    .layout = "ps2",
    .ui_scale = 1.0f,
};

static int active;
static cJSON *document;
static int dirty;
static double dirty_since;

/* ---- The data folder -------------------------------------------------------------------------- */
const char *port_data_dir(void) {
    static char path[1024];
    const char *base;

    if (path[0] != 0) {
        return path;
    }
    base = getenv("KF4_DATA_DIR");
    if (base != NULL && *base != 0) {
        snprintf(path, sizeof(path), "%s", base);
        return path;
    }
#ifdef _WIN32
    base = getenv("APPDATA");
    if (base != NULL && *base != 0) {
        snprintf(path, sizeof(path), "%s\\holyland", base);
        return path;
    }
#else
    base = getenv("XDG_CONFIG_HOME");
    if (base != NULL && *base != 0) {
        snprintf(path, sizeof(path), "%s/holyland", base);
        return path;
    }
    base = getenv("HOME");
    if (base != NULL && *base != 0) {
        snprintf(path, sizeof(path), "%s/.config/holyland", base);
        return path;
    }
#endif
    snprintf(path, sizeof(path), "holyland");
    return path;
}

const char *port_settings_path(void) {
    static char path[1100];

    if (path[0] == 0) {
        snprintf(path, sizeof(path), "%s/settings.json", port_data_dir());
    }
    return path;
}

/* Makes the folder and the ones above it */
static void make_folders(const char *folder) {
    char path[1024];
    size_t i;

    snprintf(path, sizeof(path), "%s", folder);
    for (i = 1; path[i] != 0; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            char c = path[i];

            if (path[i - 1] == ':') {
                continue; /* C:\ */
            }
            path[i] = 0;
#ifdef _WIN32
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
            path[i] = c;
        }
    }
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* ---- Reading ---------------------------------------------------------------------------------- */
static int read_bool(const cJSON *object, const char *key, int fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);

    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static double read_number(const cJSON *object, const char *key, double fallback, double low, double high) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    double value = cJSON_IsNumber(item) ? item->valuedouble : fallback;

    return value < low ? low : value > high ? high : value;
}

static const char *read_string(const cJSON *object, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);

    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int action_from_id(const char *id) {
    int action;

    for (action = 0; id != NULL && action < ACTION_COUNT; action++) {
        if (strcmp(id, port_controls_action_id(action)) == 0) {
            return action;
        }
    }
    return -1;
}

static void read_controls(const cJSON *controls) {
    const cJSON *bindings = cJSON_GetObjectItemCaseSensitive(controls, "bindings");
    const cJSON *menu = cJSON_GetObjectItemCaseSensitive(controls, "menu_mouse");
    const char *layout = read_string(controls, "layout");
    int i;

    if (layout != NULL && port_controls_layout_from_name(layout) >= 0) {
        snprintf(port_settings.layout, sizeof(port_settings.layout), "%s", layout);
    }
    port_controls_set_mouse(read_number(controls, "mouse_sensitivity", 1.0, 0.05, 10.0),
                            read_bool(controls, "invert_y", 0));
    for (i = 0; i < 3 && cJSON_IsArray(menu); i++) {
        const cJSON *item = cJSON_GetArrayItem(menu, i);

        if (cJSON_IsString(item)) {
            port_controls_set_menu_mouse(i, strcmp(item->valuestring, "none") == 0 ? -1
                                                                                     : action_from_id(item->valuestring));
        }
    }
    for (i = 0; i < CONTROLS_LAYOUTS; i++) {
        const cJSON *table = cJSON_GetObjectItemCaseSensitive(bindings, port_controls_layout_name(i));
        const cJSON *entry;

        cJSON_ArrayForEach(entry, table) {
            int action = action_from_id(entry->string);
            int slot;

            if (action < 0 || !cJSON_IsArray(entry)) {
                fprintf(stderr, "[settings] controls: no action called \"%s\"\n", entry->string);
                continue;
            }
            for (slot = 0; slot < BINDING_SLOTS; slot++) {
                const cJSON *item = cJSON_GetArrayItem(entry, slot);
                int binding;

                if (cJSON_IsString(item) && port_controls_binding_parse(item->valuestring, &binding)) {
                    port_controls_bind(i, action, slot, binding);
                } else if (item != NULL) {
                    fprintf(stderr, "[settings] controls: %s: not a key or button\n", entry->string);
                }
            }
        }
    }
}

int port_settings_active(void) {
    return active;
}

void port_settings_load(void) {
    PortSettings *s = &port_settings;
    const cJSON *display;
    const cJSON *audio;
    const cJSON *overlay;
    const char *aspect;
    char *text = NULL;
    long size = 0;
    FILE *file;

#ifdef KF4_HAVE_SDL
    active = (!port_options.headless && port_options.input == NULL) || getenv("KF4_OVERLAY_SHOT") != NULL;
#endif
    if (!active) {
        return;
    }
    file = fopen(port_settings_path(), "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        fseek(file, 0, SEEK_SET);
        text = calloc(1, (size_t)(size > 0 ? size : 0) + 1);
        if (text != NULL && size > 0 && fread(text, 1, (size_t)size, file) != (size_t)size) {
            size = 0;
        }
        fclose(file);
    }
    document = text != NULL ? cJSON_Parse(text) : NULL;
    free(text);
    if (!cJSON_IsObject(document)) {
        if (file != NULL) {
            fprintf(stderr, "[settings] %s is not valid JSON: the defaults are used, and the file is left as it is\n",
                    port_settings_path());
            active = 2; /* not written over */
        }
        cJSON_Delete(document);
        document = cJSON_CreateObject();
    }

    display = cJSON_GetObjectItemCaseSensitive(document, "display");
    s->window_scale = (int)read_number(display, "window_scale", s->window_scale, 1, 4);
    s->fullscreen = read_bool(display, "fullscreen", s->fullscreen);
    aspect = read_string(display, "aspect");
    if (aspect != NULL) {
        s->aspect = strcmp(aspect, "stretch") == 0 ? ASPECT_STRETCH : ASPECT_4_3;
    }
    s->vsync = read_bool(display, "vsync", s->vsync);
    s->smooth = read_bool(display, "smooth", s->smooth);
    s->deinterlace = read_bool(display, "deinterlace", s->deinterlace);
    s->progressive = read_bool(display, "progressive", s->progressive);
    s->anti_blur = read_bool(display, "anti_blur", s->anti_blur);
    s->adapt_width = read_bool(display, "adapt_width", s->adapt_width);
    audio = cJSON_GetObjectItemCaseSensitive(document, "audio");
    s->volume = (int)read_number(audio, "volume", s->volume, 0, 100);
    s->mute = read_bool(audio, "mute", s->mute);
    read_controls(cJSON_GetObjectItemCaseSensitive(document, "controls"));
    overlay = cJSON_GetObjectItemCaseSensitive(document, "overlay");
    s->pause_when_open = read_bool(overlay, "pause_when_open", s->pause_when_open);
    s->show_fps = read_bool(overlay, "show_fps", s->show_fps);
    s->ui_scale = (float)read_number(overlay, "scale", s->ui_scale, 0.75, 2.0);
    s->save_slot = (int)read_number(overlay, "save_slot", s->save_slot, 0, 9);

    /* the command line wins for this run: a switch it turned off stays off */
    if (port_options.deinterlace) {
        port_options.deinterlace = s->deinterlace;
    }
    if (port_options.progressive) {
        port_options.progressive = s->progressive;
    }
    if (port_options.anti_blur) {
        port_options.anti_blur = s->anti_blur;
    }
    if (port_options.adapt_width) {
        port_options.adapt_width = s->adapt_width;
    }
    if (port_options.controls == NULL) {
        port_options.controls = s->layout;
    }
    port_settings_apply_audio();
    if (file == NULL) {
        dirty = 1; /* the first run writes the defaults, so the file is there to find */
    }
    fprintf(stderr, "[settings] %s%s\n", port_settings_path(), file == NULL ? " (new)" : "");
    atexit(port_settings_save);
}

void port_settings_apply_audio(void) {
    port_audio_set_volume(port_settings.mute ? 0.0f : (float)port_settings.volume / 100.0f);
}

/* ---- Writing ---------------------------------------------------------------------------------- */
static void put(cJSON *parent, const char *key, cJSON *value) {
    if (cJSON_GetObjectItemCaseSensitive(parent, key) != NULL) {
        cJSON_ReplaceItemInObjectCaseSensitive(parent, key, value);
    } else {
        cJSON_AddItemToObject(parent, key, value);
    }
}

static cJSON *section(const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(document, key);

    if (!cJSON_IsObject(item)) {
        item = cJSON_CreateObject();
        put(document, key, item);
    }
    return item;
}

static void write_controls(cJSON *controls) {
    cJSON *bindings = cJSON_CreateObject();
    cJSON *menu = cJSON_CreateArray();
    int layout;
    int i;

    put(controls, "layout", cJSON_CreateString(port_settings.layout));
    put(controls, "mouse_sensitivity", cJSON_CreateNumber(port_controls_mouse_sensitivity()));
    put(controls, "invert_y", cJSON_CreateBool(port_controls_mouse_invert()));
    for (i = 0; i < 3; i++) {
        int action = port_controls_menu_mouse(i);

        cJSON_AddItemToArray(menu, cJSON_CreateString(action < 0 ? "none" : port_controls_action_id(action)));
    }
    put(controls, "menu_mouse", menu);
    for (layout = 0; layout < CONTROLS_LAYOUTS; layout++) {
        cJSON *table = cJSON_CreateObject();
        int action;

        for (action = 0; action < ACTION_COUNT; action++) {
            cJSON *slots;
            int slot;

            if (port_controls_is_default(layout, action)) {
                continue;
            }
            slots = cJSON_CreateArray();
            for (slot = 0; slot < BINDING_SLOTS; slot++) {
                char text[64];

                port_controls_binding_text(port_controls_binding(layout, action, slot), text, sizeof(text));
                cJSON_AddItemToArray(slots, cJSON_CreateString(text));
            }
            cJSON_AddItemToObject(table, port_controls_action_id(action), slots);
        }
        cJSON_AddItemToObject(bindings, port_controls_layout_name(layout), table);
    }
    put(controls, "bindings", bindings);
}

void port_settings_save(void) {
    const PortSettings *s = &port_settings;
    cJSON *display;
    cJSON *audio;
    cJSON *overlay;
    char temporary[1200];
    char *text;
    FILE *file;

    if (!active || !dirty) {
        return;
    }
    dirty = 0;
    if (active == 2) {
        return; /* a broken file is left for the player to mend */
    }
    display = section("display");
    put(display, "window_scale", cJSON_CreateNumber(s->window_scale));
    put(display, "fullscreen", cJSON_CreateBool(s->fullscreen));
    put(display, "aspect", cJSON_CreateString(s->aspect == ASPECT_STRETCH ? "stretch" : "4:3"));
    put(display, "vsync", cJSON_CreateBool(s->vsync));
    put(display, "smooth", cJSON_CreateBool(s->smooth));
    put(display, "deinterlace", cJSON_CreateBool(s->deinterlace));
    put(display, "progressive", cJSON_CreateBool(s->progressive));
    put(display, "anti_blur", cJSON_CreateBool(s->anti_blur));
    put(display, "adapt_width", cJSON_CreateBool(s->adapt_width));
    audio = section("audio");
    put(audio, "volume", cJSON_CreateNumber(s->volume));
    put(audio, "mute", cJSON_CreateBool(s->mute));
    write_controls(section("controls"));
    overlay = section("overlay");
    put(overlay, "pause_when_open", cJSON_CreateBool(s->pause_when_open));
    put(overlay, "show_fps", cJSON_CreateBool(s->show_fps));
    put(overlay, "scale", cJSON_CreateNumber((double)(int)(s->ui_scale * 100.0f + 0.5f) / 100.0));
    put(overlay, "save_slot", cJSON_CreateNumber(s->save_slot));

    text = cJSON_Print(document);
    if (text == NULL) {
        return;
    }
    make_folders(port_data_dir());
    /* written whole, then put in place, so a crash never leaves half a file */
    snprintf(temporary, sizeof(temporary), "%s.new", port_settings_path());
    file = fopen(temporary, "wb");
    if (file == NULL) {
        fprintf(stderr, "[settings] cannot write %s: %s\n", temporary, strerror(errno));
        cJSON_free(text);
        return;
    }
    fputs(text, file);
    fputc('\n', file);
    fclose(file);
    cJSON_free(text);
#ifdef _WIN32
    if (!MoveFileExA(temporary, port_settings_path(), MOVEFILE_REPLACE_EXISTING)) {
        fprintf(stderr, "[settings] cannot replace %s\n", port_settings_path());
    }
#else
    if (rename(temporary, port_settings_path()) != 0) {
        fprintf(stderr, "[settings] cannot replace %s: %s\n", port_settings_path(), strerror(errno));
    }
#endif
}

void port_settings_changed(void) {
    dirty = 1;
    dirty_since = port_wall_seconds();
}

void port_settings_poll(void) {
    if (dirty && port_wall_seconds() - dirty_since > 1.0) {
        port_settings_save();
    }
}
