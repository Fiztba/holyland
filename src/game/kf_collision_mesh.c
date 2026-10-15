#include "kf_collision.h"

/* the collision mesh a model has, read through the model each time (the copy of the pointer that
   retail keeps in a register comes from that) */
#define MODEL_MESH(model) ((CollisionMesh *)(model)->strips)

/* Does nothing. */
void func_001123F8(void) {
}

/* Makes a collision mesh that was just loaded usable: turns its offsets into pointers and, if
   asked, its point numbers into scratchpad addresses. Returns where the mesh ends. */
char *func_00112400(char *data, ModelInstance *model, int use_scratchpad) {
    char *cursor = data;
    CollisionStrip *strip;
    CollisionStripPoint *point;
    int *offsets;
    int i;
    int j;

    model->strips = (ModelStripList *)cursor;
    cursor += 0x50;
    MODEL_MESH(model)->strips = (CollisionStrip *)cursor;
    strip = (CollisionStrip *)cursor;
    /* the six strip lists and the two words after them are offsets from the mesh */
    offsets = (int *)&MODEL_MESH(model)->unk18;
    for (i = 0; i < 8; i++) {
        offsets[i] = (int)data + offsets[i];
    }
    for (i = 0; (u32)i < MODEL_MESH(model)->strip_count; i++) {
        point = strip->points;
        cursor += 0x20;
        if (use_scratchpad) {
            for (j = 0; j < strip->point_count; j++) {
                point->position = &COLLISION_WORLD_POINTS[(int)point->position];
                point++;
            }
        } else {
            point += strip->point_count;
        }
        cursor += strip->point_count * sizeof(CollisionStripPoint);
        strip = (CollisionStrip *)point;
    }
    MODEL_MESH(model)->unk08 = cursor;
    return cursor + MODEL_MESH(model)->unk0C * 0x10;
}

/* Surface attribute: the third value of (0, 0xFF, 0-7) or (0xFF, 0, 0-7), the second of (0xFF, 0-7, 0xFF),
   5 for (0xFF, 0xFF, 0xFE-0xFF), 6 otherwise. */
u32 func_001124E0(int first, u32 second, u32 third) {
    if (first == 0 && second == 0xFF && third < 8) {
        return third;
    }
    if (first == 0xFF && second == 0 && third < 8) {
        return third;
    }
    if (first == 0xFF && second < 8 && third == 0xFF) {
        return second;
    }
    if (first == 0xFF && second == 0xFF && third >= 0xFE) {
        return 5;
    }
    return 6;
}

/* Surface attribute: the first value of (0-7, 0xFF, 0xFF), the third of (0xFF, 0xFF, 0xFE-0xFF), 0 otherwise. */
u32 func_00112560(u32 first, u32 second, u32 third) {
    if (first < 8 && second == 0xFF && third == 0xFF) {
        return first;
    }
    if (first == 0xFF && second == 0xFF && third >= 0xFE) {
        return third;
    }
    return 0;
}

/* Surface attribute: the first value when the triple is (0-7, 0xFF, 0xFF), 6 otherwise. */
u32 func_001125A8(u32 first, u32 second, int third) {
    if (first < 8 && second == 0xFF && third == 0xFF) {
        return first;
    }
    return 6;
}

/* Surface attribute: the third value when the triple is (0xFF, 0xFF, 0xFE-0xFF), 1 otherwise. */
u32 func_001125D0(u32 first, u32 second, u32 third) {
    if (first == 0xFF && second == 0xFF && third >= 0xFE) {
        return third;
    }
    return 1;
}

/* Surface attribute: the first value when the triple is (0-7, 0-1, 0xFF), 0 otherwise. */
u32 func_001125F0(u32 first, u32 second, int third) {
    if (first < 8 && second < 2 && third == 255) {
        return first;
    }
    return 0;
}

/* Surface attribute: the second value when the triple is (0-7, 0-1, 0xFF), 0xFFFFFFFF otherwise. */
u32 func_00112618(u32 first, u32 second, int third) {
    if (first < 8 && second < 2 && third == 0xFF) {
        return second;
    }
    return 0xFFFFFFFF;
}
