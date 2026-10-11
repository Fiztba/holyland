float atan2f(float, float);
float sqrtf(float);

void func_001955E0(float *arg0, float *arg1) {
    float x;

    arg0[0] = -atan2f(arg1[1], sqrtf(arg1[0] * arg1[0] + arg1[2] * arg1[2]));
    x = -atan2f(arg1[2], arg1[0]) + 1.5707964f;
    x = x - (float)(int)(x / 6.2831855f) * 6.2831855f;
    if (x > 3.1415927f) {
        x = x - 6.2831855f;
    } else if (x < -3.1415927f) {
        x = x + 6.2831855f;
    }
    arg0[1] = x;
    arg0[2] = arg0[3] = 0.0f;
}
