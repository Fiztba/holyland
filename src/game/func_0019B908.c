float func_0019B838(float);
float func_0019B898(float);

int func_0019B908(float *p, float target, float step) {
    float a, b;

    *p = func_0019B838(*p);
    target = func_0019B838(target);
    if (!(0.0f <= step)) {
        step = -step;
    }
    if (*p == target) {
        return 1;
    }
    a = func_0019B898(target - *p);
    b = func_0019B898(*p - target);
    if (a < b) {
        if (a <= step) {
            *p = target;
            return 1;
        }
        *p = *p + step;
    } else {
        if (b <= step) {
            *p = target;
            return 1;
        }
        *p = *p - step;
    }
    return 0;
}
