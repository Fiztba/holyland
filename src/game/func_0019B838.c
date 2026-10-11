float func_0019B838(float arg0) {
    float var_f12;
    int n;

    n = arg0 / 6.2831855f;
    var_f12 = arg0 - (float)n * 6.2831855f;
    if (3.1415927f < var_f12) {
        var_f12 -= 6.2831855f;
    } else if (var_f12 < -3.1415927f) {
        var_f12 += 6.2831855f;
    }
    return var_f12;
}
