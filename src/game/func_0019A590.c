void func_00216CF0(void *, void *, void *, void *, void *, float *);

void func_0019A590(float arg0, void *arg1, void *arg2, void *arg3, void *arg4, void *arg5) {
    float v[4];
    float t2 = arg0 * arg0;
    float t3 = t2 * arg0;

    v[0] = t3 + t3 - 3.0f * t2 + 1.0f;
    v[1] = t3 * -2.0f + 3.0f * t2;
    v[2] = t3 - 2.0f * t2 + arg0;
    v[3] = t3 - t2;
    func_00216CF0(arg1, arg2, arg3, arg4, arg5, v);
}
