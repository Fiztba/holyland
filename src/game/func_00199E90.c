typedef struct {
    char pad0[0xC];
    char *ctrl;
} Actor;

float func_00199E90(Actor *arg0, float arg1) {
    float var_f0;

    var_f0 = arg1 - *(float *)(arg0->ctrl + 0x50);
    if (var_f0 < 0.0f) {
        var_f0 = 0.0f;
    }
    return var_f0;
}
