typedef struct {
    char pad[0x50];
    float lo;
    float hi;
} Ctrl;

typedef struct {
    char pad[0xC];
    Ctrl *ctrl;
} Actor;

float func_00199E68(Actor *arg0, float arg1) {
    Ctrl *c = arg0->ctrl;
    float t = c->hi;
    float s = arg1 + c->lo;
    if (t < s) {
        s = t;
    }
    return s;
}
