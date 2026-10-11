int rand(void);

float func_00195818(void) {
    float var_f2;

    var_f2 = (float) (rand() % 360) * 0.017453292f;
    if (3.1415927f <= var_f2) {
        var_f2 -= 6.2831855f;
    }
    return var_f2;
}
