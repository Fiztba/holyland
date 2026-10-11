/* MATCHES (kf diff OK) with float literals; the extern const lit4 version does not
   (a literal load is a const_double move whose length keeps reorg from stealing it
   into the bc1f delay slot). Retail .lit4 D_00445D0C..D_00445D20 are exactly these
   six literals in load order. */
float func_00195388(float *arg0, float arg1, float arg2) {
    float old = *arg0;
    float d = arg1 - old;

    d = d - (float)(int)(d / 6.2831855f) * 6.2831855f;
    if (d > 3.1415927f) {
        d = d - 6.2831855f;
    } else if (d < -3.1415927f) {
        d = d + 6.2831855f;
    }
    if (arg2 <= 1.0f) {
        *arg0 = arg1;
        return d;
    }
    arg2 = old + d / arg2;
    arg2 = arg2 - (float)(int)(arg2 / 6.2831855f) * 6.2831855f;
    if (arg2 > 3.1415927f) {
        arg2 = arg2 - 6.2831855f;
    } else if (arg2 < -3.1415927f) {
        arg2 = arg2 + 6.2831855f;
    }
    *arg0 = arg2;
    return d;
}
