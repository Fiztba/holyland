/* The parts of the PS2's C and compiler runtime whose results the game depends on, where the host's
   differ: rand (the SDK's newlib, RAND_MAX 0x7FFFFFFF, seed 1), mktime (newlib's, on the game's
   9-int struct tm) and fptoui (libgcc's soft-float conversion, which saturates where a C cast is
   undefined). */
#include <stdint.h>

static int32_t rand_next = 1;

void srand(unsigned int seed) {
    rand_next = (int32_t)seed;
}

/* newlib's rand as the SDK built it: next = next * 1103515245 + 12345, the low 31 bits */
int rand(void) {
    rand_next = (int32_t)((uint32_t)rand_next * 0x41C64E6Du + 0x3039u);
    return rand_next & 0x7FFFFFFF;
}

/* libgcc's float -> unsigned int: 0 for NaN, negatives and values below 1, all ones from 2^32 up */
unsigned int fptoui(float value) {
    if (!(value >= 1.0f)) {
        return 0;
    }
    if (value >= 4294967296.0f) {
        return 0xFFFFFFFFu;
    }
    return (unsigned int)value;
}

/* newlib's mktime as the SDK built it, with no time zone set: normalises the fields, fills in the
   day of the week and of the year, clears tm_isdst, and returns seconds since 1970 as the EE's
   32-bit time_t. fields is the game's 9-int struct tm (CalendarTime): the host's struct tm can be
   larger (glibc adds tm_gmtoff and tm_zone), so the host's mktime would write past it. */
static int days_in_month(int year, int month) {
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y = year + 1900;
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return days[month] + (month == 1 && leap);
}

static int days_in_year(int year) {
    return 365 + (days_in_month(year, 1) == 29);
}

/* a / b and a % b rounded towards minus infinity, as newlib's normalisation does */
static void carry(int *low, int *high, int range) {
    int q = *low / range;
    int r = *low % range;
    if (r < 0) {
        r += range;
        q--;
    }
    *low = r;
    *high += q;
}

int ps2_mktime(int *fields) {
    enum { SEC, MIN, HOUR, MDAY, MON, YEAR, WDAY, YDAY, ISDST };
    int64_t days;
    int64_t seconds;
    int year;
    int month;

    carry(&fields[SEC], &fields[MIN], 60);
    carry(&fields[MIN], &fields[HOUR], 60);
    carry(&fields[HOUR], &fields[MDAY], 24);
    carry(&fields[MON], &fields[YEAR], 12);
    while (fields[MDAY] <= 0) {
        if (--fields[MON] < 0) {
            fields[MON] = 11;
            fields[YEAR]--;
        }
        fields[MDAY] += days_in_month(fields[YEAR], fields[MON]);
    }
    while (fields[MDAY] > days_in_month(fields[YEAR], fields[MON])) {
        fields[MDAY] -= days_in_month(fields[YEAR], fields[MON]);
        if (++fields[MON] > 11) {
            fields[MON] = 0;
            fields[YEAR]++;
        }
    }

    fields[YDAY] = fields[MDAY] - 1;
    for (month = 0; month < fields[MON]; month++) {
        fields[YDAY] += days_in_month(fields[YEAR], month);
    }
    if (fields[YEAR] > 10000 || fields[YEAR] < -10000) {
        return -1;
    }
    days = fields[YDAY];
    for (year = 70; year < fields[YEAR]; year++) {
        days += days_in_year(year);
    }
    for (year = fields[YEAR]; year < 70; year++) {
        days -= days_in_year(year);
    }
    fields[WDAY] = (int)(((days + 4) % 7 + 7) % 7); /* 1 January 1970 was a Thursday */
    fields[ISDST] = 0;
    seconds = days * 86400 + fields[HOUR] * 3600 + fields[MIN] * 60 + fields[SEC];
    return (int)(uint32_t)seconds;
}
