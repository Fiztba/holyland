int func_001B49F8(int arg0, int arg1, int arg2, int arg3) {
    float t0;
    float t5;
    float a;
    float b;
    float tmp;
    float r;

    t0 = (float)arg1;
    t5 = (float)arg3;
    a = ((t0 + t0) - t5) * 0.5f;
    tmp = t0 + ((t0 * (float)arg2) / 50.0f);
    b = ((tmp + tmp) - t5) * 0.5f;
    r = ((float)arg0 * (200.0f - t5)) / 200.0f;
    if (a > 0.0f) {
        r = r + a * 0.6f;
    }
    if (b > 0.0f) {
        r = r + b;
    }
    return (int)r;
}
