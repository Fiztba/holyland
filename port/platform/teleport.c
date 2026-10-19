/* Teleport (port/include/port/teleport.h): the maps on the disc, the places in each, read from the map
   files as tools/map_tour.py reads them (map_places, map_floors there), and the requests, which are
   quick loads (port/platform/quicksave.c, game/quicksave.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/platform.h"
#include "port/quicksave.h"
#include "port/teleport.h"

#define MAX_MAPS 128
#define TOC_SIZE 0x20000u

/* ---- The map file (map/map.h: MapSlotUnk08, MapRecordBounds) ---------------------------------- */
#define MAP_RECORDS 0x17C0u
#define MAP_RECORD_SIZE 0x80u
#define RECORD_NO_COLLISION 0x40000000u
#define PLACE_SPACING 1500.0 /* places are at least this far apart (x and z) */

/* The names decomp/docs/GAME_DATA.md gives the maps (Prima's guide) */
static const struct {
    int map;
    const char *name;
} map_names[] = {
    {0, "Village Ruins"},
    {2, "Miner's Cemetery"},
    {4, "East Valley"},
    {5, "Catacombs (Giant Spider's Lair)"},
    {6, "The King's Throne, Upper"},
    {7, "The King's Throne, Lower"},
    {8, "Upper Mines"},
    {9, "Lower Mines"},
    {11, "The Armory"},
    {12, "The Ancient City Exterior"},
    {13, "The Holy Forest"},
    {14, "The Heart of the Forest"},
    {15, "The Ancient City, Level 3, West"},
    {16, "South Cliffs"},
    {17, "The Forge"},
    {18, "The Forge"},
    {19, "The Forge"},
    {20, "The Earth Folks' Village"},
    {21, "The Ocean Fortress"},
    {22, "The Queen's Palace"},
    {23, "The Widda Queen's Lair"},
    {24, "The Cavern of Lava"},
    {25, "The Fire Lord's Domain"},
    {26, "Cliff Passage"},
    {27, "Ocean Shore"},
    {28, "Seabed Cave"},
    {29, "The Underground Canal"},
    {30, "The Passage of Light"},
    {31, "North Cliffs"},
    {32, "The Egg Mines"},
    {33, "The Mansion of Howling Winds, Exterior"},
    {34, "The Mansion of Howling Winds, Interior"},
    {35, "The Guardian's Room"},
    {36, "The Underground Jail"},
    {37, "The Old Battlefield"},
    {38, "The Dark Sanctuary"},
    {39, "The Dark Reality, Entrance"},
    {40, "The Dark Reality, Conclusion"},
    {41, "The King's Throne, Alcoves"},
    {42, "The Sanctuary of Light"},
    {43, "The Queen's Palace, Catacombs"},
    {44, "The Snake Dens"},
    {95, "Upper and Lower Underground Graveyard"},
    {96, "The Shop"},
    {97, "Icon of Healing"},
    {98, "Palace of Oath Interior"},
    {99, "Palace of Oath Exterior"},
    {100, "The Ancient City, Level 3, Central Tower"},
    {101, "The Ancient City, Level 3, North"},
    {102, "The Ancient City, Level 3, East"},
    {103, "The Ancient City, Level 3, East Catacombs"},
    {104, "The Ancient City, Level 3, South, Craftsman's Room"},
    {105, "The Ancient City, Level 3, South"},
    {106, "The Ancient City, Level 3, South, Widda Areas"},
    {107, "The Ancient City, Level 2, Central Tower"},
    {108, "The Ancient City, Level 2, South"},
    {109, "The Ancient City, Level 2, West"},
    {110, "The Ancient City, Level 2, West, Sorcerer's Area"},
    {111, "The Ancient City, Level 2, North"},
    {112, "The Ancient City, Level 1, Central Tower"},
    {113, "The Ancient City, Level 1, West"},
    {114, "The Ruins of the Dark Folk"},
    {115, "The Ancient City, Level 1 and 2, East"},
    {116, "The Treasury"},
};

typedef struct {
    int map;
    uint32_t lsn, size;
    int read;  /* its places were worked out */
    int place_count;
    PortTeleportPlace places[PORT_TELEPORT_PLACES];
} MapEntry;

static MapEntry maps[MAX_MAPS];
static int map_count = -1; /* -1: KF4.DAT not read yet */

/* --teleport */
static uint64_t command_vblank;
static int command_map = -1, command_place;

static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static float lef(const uint8_t *p) {
    uint32_t bits = le32(p);
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

/* KF4.DAT's table: the NNN.map members, in order */
static void read_maps(void) {
    uint32_t lsn, size, count, i;
    uint8_t *toc;

    map_count = 0;
    if (!port_disc_find("\\DATA\\KF4.DAT", &lsn, &size) || (toc = malloc(TOC_SIZE)) == NULL) {
        return;
    }
    if (port_disc_read(lsn, TOC_SIZE / DISC_SECTOR_SIZE, toc)) {
        count = le32(toc + 4);
        for (i = 0; i < count && 0x40 + (i + 1) * 0x40 <= TOC_SIZE && map_count < MAX_MAPS; i++) {
            const uint8_t *entry = toc + 0x40 + i * 0x40;
            char name[0x35];
            int number;
            char tail;

            memcpy(name, entry, 0x34);
            name[0x34] = 0;
            if (strlen(name) == 7 && sscanf(name, "%3d.ma%c", &number, &tail) == 2 && tail == 'p' &&
                name[0] >= '0' && name[0] <= '9') {
                MapEntry *m = &maps[map_count++];

                memset(m, 0, sizeof(*m));
                m->map = number;
                m->lsn = lsn + le32(entry + 0x3C) / DISC_SECTOR_SIZE;
                m->size = le32(entry + 0x34);
            }
        }
    }
    free(toc);
    /* in number order */
    for (i = 1; i < (uint32_t)map_count; i++) {
        MapEntry m = maps[i];
        int j = (int)i - 1;

        while (j >= 0 && maps[j].map > m.map) {
            maps[j + 1] = maps[j];
            j--;
        }
        maps[j + 1] = m;
    }
}

static MapEntry *find_map(int map) {
    int i;

    if (map_count < 0) {
        read_maps();
    }
    for (i = 0; i < map_count; i++) {
        if (maps[i].map == map) {
            return &maps[i];
        }
    }
    return NULL;
}

/* ---- The places (tools/map_tour.py: map_places) ---------------------------------------------- */
typedef struct {
    double p[3][3];
    int record;
} Triangle;

typedef struct {
    Triangle *items;
    int count, room;
} Triangles;

static void push(Triangles *list, const Triangle *t) {
    if (list->count == list->room) {
        int room = list->room ? list->room * 2 : 256;
        Triangle *grown = realloc(list->items, (size_t)room * sizeof(Triangle));

        if (grown == NULL) {
            return;
        }
        list->items = grown;
        list->room = room;
    }
    list->items[list->count++] = *t;
}

/* Whether [at, at + n) is in the file */
static int inside(uint32_t size, uint32_t at, uint32_t n) {
    return at <= size && n <= size - at;
}

/* The triangles of strip list `which` of the collision mesh at `at` (CollisionMesh as
   Collision_RelocateMesh reads it: a 0x50-byte head, the strips, then the points; a strip point names a
   point by number), scaled and moved as the record says. 0 when the mesh does not read right. */
static int mesh_triangles(const uint8_t *data, uint32_t size, uint32_t at, int which, const double scale[3],
                          const double move[3], int record, Triangles *out) {
    int32_t strip_count, i, k;
    uint32_t point_count, cursor, points, list;
    uint16_t count;

    if (!inside(size, at, 0x50)) {
        return 0;
    }
    strip_count = (int32_t)le32(data + at);
    point_count = le32(data + at + 0x0C);
    cursor = at + 0x50;
    for (i = 0; i < strip_count; i++) {
        if (!inside(size, cursor, 0x20)) {
            return 0;
        }
        cursor += 0x20 + le16(data + cursor) * 16u;
    }
    points = cursor;
    if (!inside(size, points, point_count * 16u)) {
        return 0;
    }
    list = at + le32(data + at + 0x18 + which * 4);
    count = le16(data + at + 0x38 + which * 2);
    for (i = 0; i < count; i++) {
        uint16_t n;
        double corner[3][3];

        if (!inside(size, list, 0x20)) {
            return 0;
        }
        n = le16(data + list);
        if (!inside(size, list + 0x20, n * 16u)) {
            return 0;
        }
        for (k = 0; k < n; k++) {
            uint32_t index = le32(data + list + 0x20 + 16u * (uint32_t)k);
            int axis;

            if (index >= point_count) {
                return 0;
            }
            for (axis = 0; axis < 3; axis++) {
                corner[k % 3][axis] = lef(data + points + 16u * index + 4u * (uint32_t)axis) * scale[axis] + move[axis];
            }
            if (k >= 2) {
                Triangle t;

                /* the strip's last three points, in order */
                memcpy(t.p[0], corner[(k - 2) % 3], sizeof(t.p[0]));
                memcpy(t.p[1], corner[(k - 1) % 3], sizeof(t.p[1]));
                memcpy(t.p[2], corner[k % 3], sizeof(t.p[2]));
                t.record = record;
                push(out, &t);
            }
        }
        list += 0x20 + n * 16u;
    }
    return 1;
}

/* The floor (strip list 1) and hazard (list 4: lava, poison) triangles of the map's records, in the
   map's space. Only records that are not turned (a scale and a move: their mesh then fits their box,
   which is checked). */
static void map_floors(const uint8_t *data, uint32_t size, Triangles *floors, Triangles *hazards) {
    uint16_t records = le16(data + 0x3C);
    uint32_t starts[256];
    int known = 1, r;

    starts[0] = le32(data + 0x08);
    for (r = 0; r < records; r++) {
        uint32_t at = MAP_RECORDS + (uint32_t)r * MAP_RECORD_SIZE;
        double move[3], scale[3], low[3], high[3];
        uint16_t mesh;
        uint32_t flags;
        int axis, turned = 0, fits = 1, i, floor_first, hazard_first;

        if (!inside(size, at, MAP_RECORD_SIZE)) {
            return;
        }
        for (axis = 0; axis < 3; axis++) {
            move[axis] = lef(data + at + 4u * axis);
            turned |= fabs(lef(data + at + 0x10 + 4u * axis)) > 1e-4;
            scale[axis] = lef(data + at + 0x20 + 4u * axis);
            if (scale[axis] == 0.0) {
                scale[axis] = 1.0;
            }
            low[axis] = lef(data + at + 0x60 + 4u * axis);
            high[axis] = lef(data + at + 0x70 + 4u * axis);
        }
        mesh = le16(data + at + 0x32);
        flags = le32(data + at + 0x3C);
        if (mesh == 0xFFFF || (flags & RECORD_NO_COLLISION) || turned || mesh >= 256) {
            continue;
        }
        /* the meshes are back to back (Map_BuildModels): each gives its size */
        while (known <= mesh) {
            if (!inside(size, starts[known - 1], 0x18)) {
                return;
            }
            starts[known] = starts[known - 1] + le32(data + starts[known - 1] + 0x14);
            known++;
        }
        floor_first = floors->count;
        hazard_first = hazards->count;
        if (!mesh_triangles(data, size, starts[mesh], 1, scale, move, r, floors) ||
            !mesh_triangles(data, size, starts[mesh], 4, scale, move, r, hazards)) {
            floors->count = floor_first;
            hazards->count = hazard_first;
            continue;
        }
        /* not where its box says: read otherwise than thought, so left out */
        for (i = floor_first; i < floors->count && fits; i++) {
            int c;

            for (c = 0; c < 3; c++) {
                for (axis = 0; axis < 3; axis++) {
                    fits &= floors->items[i].p[c][axis] >= low[axis] - 2 && floors->items[i].p[c][axis] <= high[axis] + 2;
                }
            }
        }
        for (i = hazard_first; i < hazards->count && fits; i++) {
            int c;

            for (c = 0; c < 3; c++) {
                for (axis = 0; axis < 3; axis++) {
                    fits &= hazards->items[i].p[c][axis] >= low[axis] - 2 && hazards->items[i].p[c][axis] <= high[axis] + 2;
                }
            }
        }
        if (!fits) {
            floors->count = floor_first;
            hazards->count = hazard_first;
        }
    }
}

/* The triangle's y over (x, z), in *y: 0 when (x, z) is not under it */
static int height_at(const Triangle *t, double x, double z, double *y) {
    const double *a = t->p[0], *b = t->p[1], *c = t->p[2];
    double det = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
    double u, v;

    if (fabs(det) < 1e-6) {
        return 0;
    }
    u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / det;
    v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / det;
    if (u < -1e-4 || v < -1e-4 || u + v > 1 + 1e-4) {
        return 0;
    }
    *y = u * a[1] + v * b[1] + (1 - u - v) * c[1];
    return 1;
}

static double area(const Triangle *t) {
    return fabs((t->p[1][0] - t->p[0][0]) * (t->p[2][2] - t->p[0][2]) -
                (t->p[2][0] - t->p[0][0]) * (t->p[1][2] - t->p[0][2])) / 2;
}

static const Triangle *sort_floors;

static int by_area(const void *a, const void *b) {
    double x = area(&sort_floors[*(const int *)a]), y = area(&sort_floors[*(const int *)b]);

    return x > y ? -1 : x < y ? 1 : *(const int *)a - *(const int *)b;
}

/* Takes a place: not on a hazard, and far enough from those taken */
static void take(MapEntry *m, const Triangles *hazards, double x, double floor_y, double z, const char *label) {
    int i;

    if (m->place_count >= PORT_TELEPORT_PLACES) {
        return;
    }
    for (i = 0; i < m->place_count; i++) {
        if (hypot(x - m->places[i].position[0], z - m->places[i].position[2]) < PLACE_SPACING) {
            return;
        }
    }
    for (i = 0; i < hazards->count; i++) {
        double y;

        if (height_at(&hazards->items[i], x, z, &y) && fabs(y - floor_y) < 300) {
            return;
        }
    }
    {
        PortTeleportPlace *place = &m->places[m->place_count++];

        place->position[0] = (float)x;
        place->position[1] = (float)(floor_y - 50.0); /* y grows downward: from a little over it */
        place->position[2] = (float)z;
        place->heading = 0.0f;
        snprintf(place->label, sizeof(place->label), "%s", label);
    }
}

static void read_places(MapEntry *m) {
    uint32_t sectors = (m->size + DISC_SECTOR_SIZE - 1) / DISC_SECTOR_SIZE;
    uint8_t *data;
    Triangles floors = {0}, hazards = {0};
    uint16_t records, flag_blocks, objects, spawns, items;
    uint32_t item_table, i;
    int *order;

    m->read = 1;
    m->place_count = 0;
    if (m->size < MAP_RECORDS || (data = malloc((size_t)sectors * DISC_SECTOR_SIZE)) == NULL) {
        return;
    }
    if (!port_disc_read(m->lsn, sectors, data)) {
        free(data);
        return;
    }
    /* the map's start (MapSlotUnk08.player_start_*), where a map change without a link puts the player */
    m->places[0].position[0] = lef(data + 0x60);
    m->places[0].position[1] = lef(data + 0x64);
    m->places[0].position[2] = lef(data + 0x68);
    m->places[0].heading = lef(data + 0x6C);
    snprintf(m->places[0].label, sizeof(m->places[0].label), "start");
    m->place_count = 1;

    records = le16(data + 0x3C);
    flag_blocks = le16(data + 0x3E);
    objects = le16(data + 0x40);
    spawns = le16(data + 0x42);
    items = le16(data + 0x44);
    map_floors(data, m->size, &floors, &hazards);

    /* the fixed items (MapItemDef: they lie where the player picks them up) that lie on a floor.
       Map_ParseFile: the records, the flag blocks, the 32 origins, the object placements, the spawns,
       the items */
    item_table = MAP_RECORDS + records * MAP_RECORD_SIZE + flag_blocks * 0x40u + 0x200u + objects * 0x80u + spawns * 0x60u;
    for (i = 0; i < items && inside(m->size, item_table + i * 0x40u, 12); i++) {
        double x = lef(data + item_table + i * 0x40u), y = lef(data + item_table + i * 0x40u + 4);
        double z = lef(data + item_table + i * 0x40u + 8), best = 0.0, h;
        int found = 0, t;
        char label[32];

        for (t = 0; t < floors.count; t++) {
            if (height_at(&floors.items[t], x, z, &h) && y - 50 <= h && h <= y + 400 && (!found || h < best)) {
                best = h;
                found = 1;
            }
        }
        if (found) {
            snprintf(label, sizeof(label), "item %u", (unsigned)i);
            take(m, &hazards, x, best, z, label);
        }
    }

    /* the middles of the biggest floor triangles */
    order = malloc((size_t)(floors.count ? floors.count : 1) * sizeof(int));
    if (order != NULL) {
        int t;

        for (t = 0; t < floors.count; t++) {
            order[t] = t;
        }
        sort_floors = floors.items;
        qsort(order, (size_t)floors.count, sizeof(int), by_area);
        for (t = 0; t < floors.count; t++) {
            const Triangle *tri = &floors.items[order[t]];
            char label[32];

            if (area(tri) < 200.0 * 200.0) {
                break;
            }
            snprintf(label, sizeof(label), "floor %d", tri->record);
            take(m, &hazards, (tri->p[0][0] + tri->p[1][0] + tri->p[2][0]) / 3,
                 (tri->p[0][1] + tri->p[1][1] + tri->p[2][1]) / 3, (tri->p[0][2] + tri->p[1][2] + tri->p[2][2]) / 3,
                 label);
        }
        free(order);
    }
    free(floors.items);
    free(hazards.items);
    free(data);
}

/* ---- The interface --------------------------------------------------------------------------- */
int port_teleport_map_count(void) {
    if (map_count < 0) {
        read_maps();
    }
    return map_count;
}

int port_teleport_map(int n) {
    return n >= 0 && n < port_teleport_map_count() ? maps[n].map : -1;
}

const char *port_teleport_map_name(int map) {
    size_t i;

    for (i = 0; i < sizeof(map_names) / sizeof(map_names[0]); i++) {
        if (map_names[i].map == map) {
            return map_names[i].name;
        }
    }
    return "";
}

int port_teleport_places(int map, PortTeleportPlace *places) {
    MapEntry *m = find_map(map);

    if (m == NULL) {
        return 0;
    }
    if (!m->read) {
        /* the same places whoever asks first, the overlay or --teleport on the game's fiber */
        uint32_t mode = port_host_float_begin();

        read_places(m);
        port_host_float_end(mode);
    }
    memcpy(places, m->places, (size_t)m->place_count * sizeof(PortTeleportPlace));
    return m->place_count;
}

static int request(int map, int place, int patient) {
    PortTeleportPlace places[PORT_TELEPORT_PLACES];
    int count = port_teleport_places(map, places);

    if (place < 0 || place >= count) {
        return 0;
    }
    port_quick_request_teleport(map, places[place].position, places[place].heading, places[place].label, patient);
    return 1;
}

int port_teleport_request(int map, int place) {
    return request(map, place, 0);
}

void port_teleport_init(void) {
    const char *text = port_options.teleport;
    char *end;
    unsigned long long first;

    if (text == NULL) {
        return;
    }
    /* [VBLANK:]MAP[:PLACE] */
    first = strtoull(text, &end, 10);
    if (end != text && *end == ':') {
        const char *rest = end + 1;
        long second = strtol(rest, &end, 10);

        if (end != rest && *end == ':') {
            command_vblank = first;
            command_map = (int)second;
            rest = end + 1;
            command_place = (int)strtol(rest, &end, 10);
        } else if (end != rest && *end == 0) {
            /* MAP:PLACE: from the first moment the game is in play */
            command_map = (int)first;
            command_place = (int)second;
        }
    } else if (end != text && *end == 0) {
        command_map = (int)first;
    }
    if (command_map < 0 || *end != 0 || end == text) {
        fprintf(stderr, "--teleport wants [VBLANK:]MAP[:PLACE] (VBLANK:MAP:PLACE when a vblank is given), not %s\n", text);
        exit(2);
    }
}

void port_teleport_vblank(void) {
    if (command_map >= 0 && port_vblank_count() >= command_vblank && port_quick_wanted() == PORT_QUICK_NONE) {
        if (!request(command_map, command_place, 1)) {
            fprintf(stderr, "--teleport: map %d has no place %d\n", command_map, command_place);
        }
        command_map = -1;
    }
}
