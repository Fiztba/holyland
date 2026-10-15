#include "kf_game.h"

#define BCD2DEC(x) (((x) & 0xF) + ((x) >> 4) * 10)

int func_001D25C8(ClockTime *);
int func_001D2298(void);
int func_001D22B8(void);
int func_001D23F0(ClockTime, ClockTime);
int func_001D24A8(ClockTime, ClockTime);
int func_001D2550(ClockTime);
int func_001D2590(ClockTime);
int mktime(void *);
int func_001D2100(void);
int func_001D2390(ClockTime);
int sceCdReadClock(ClockTime *);

extern ClockTime D_00446A10;
extern ClockTime D_00446A18;
extern ClockTime D_00446A20;
extern ClockTime D_00446A28;

/* Restart the play clock from a saved play time: keep it as the base and read the wall clock. */
void func_001D2048(ClockTime play_time) {
    D_00446A18.sec = play_time.sec;
    D_00446A18.min = play_time.min;
    D_00446A18.hour = play_time.hour;
    D_00446A18.mday = play_time.mday;
    D_00446A18.mon = play_time.mon;
    D_00446A18.year = play_time.year;
    func_001D25C8(&D_00446A10);
}

/* Start the play clock from zero. */
int func_001D20A0(void) {
    D_00446A18.sec = 0;
    D_00446A18.min = 0;
    D_00446A18.hour = 0;
    D_00446A18.mday = 0;
    D_00446A18.mon = 0;
    D_00446A18.year = 0;
    func_001D25C8(&D_00446A10);
    return 0;
}

void func_001D20E0(void) {
    func_001D2298();
    func_001D22B8();
}

/* Seconds of wall-clock time between the clock start and the last clock read. */
int func_001D2100(void) {
    return func_001D23F0(D_00446A10, D_00446A20);
}

/* Minutes of wall-clock time between the clock start and the last clock read. */
int func_001D2138(void) {
    return func_001D24A8(D_00446A10, D_00446A20);
}

/* Seconds from the given play time to the current play time. */
int func_001D2170(ClockTime play_time) {
    return func_001D2550(D_00446A28) - func_001D2550(play_time);
}

/* Minutes from the given play time to the current play time. */
int func_001D21B8(ClockTime play_time) {
    return func_001D2590(D_00446A28) - func_001D2590(play_time);
}

/* The day of the week of the last clock read, 0: Sunday. */
int func_001D2200(void) {
    CalendarTime calendar;

    calendar.tm_sec = D_00446A20.sec;
    calendar.tm_min = D_00446A20.min;
    calendar.tm_hour = D_00446A20.hour;
    calendar.tm_mday = D_00446A20.mday;
    calendar.tm_mon = D_00446A20.mon - 1;
    calendar.tm_year = D_00446A20.year + 0x64;
    mktime(&calendar);
    return calendar.tm_wday;
}

/* Seconds from one play time to another. */
int func_001D2258(ClockTime start, ClockTime end) {
    int start_seconds = func_001D2550(start);

    return func_001D2550(end) - start_seconds;
}

/* Read the wall clock into D_00446A20; gives sceCdReadClock's result. */
int func_001D2298(void) {
    return func_001D25C8(&D_00446A20);
}

/* The play time now: the saved base plus the wall-clock seconds since the start, into D_00446A28. */
int func_001D22B8(void) {
    int elapsed;
    int seconds;
    int minutes;
    int hours;

    elapsed = func_001D2100();
    seconds = elapsed + func_001D2550(D_00446A18);
    D_00446A28.sec = seconds % 60;
    minutes = seconds / 60;
    D_00446A28.min = minutes % 60;
    hours = minutes / 60;
    D_00446A28.hour = hours % 99;
    D_00446A28.mday = hours / 99;
    return 1;
}

/* A play time (seconds, minutes, hours, and hundreds of hours in mday) as seconds. */
int func_001D2350(ClockTime play_time) {
    return play_time.sec + (play_time.min + (play_time.hour + play_time.mday * 99) * 60) * 60;
}

int func_001D2390(ClockTime time) {
    int tm_fields[9];

    tm_fields[0] = time.sec;
    tm_fields[1] = time.min;
    tm_fields[2] = time.hour;
    tm_fields[3] = time.mday;
    tm_fields[4] = time.mon - 1;
    tm_fields[5] = time.year + 0x64;
    mktime(tm_fields);
    return tm_fields[7];
}

/* Seconds from clock start to clock end (same century). */
int func_001D23F0(ClockTime start, ClockTime end) {
    int start_day;
    int end_day;
    char buf[0x30]; /* unused, but retail reserves the 0x30 bytes */

    start_day = func_001D2390(start);
    end_day = func_001D2390(end);
    if (start.year != end.year) {
        int year = start.year;

        if (year & 3) {
            end_day += (end.year - year) * 365;
        } else {
            end_day += (end.year - year) * 366;
        }
    }
    return (end.sec - start.sec)
        + ((end.min - start.min) + ((end.hour - start.hour) + (end_day - start_day) * 24) * 60) * 60;
}

/* Minutes from clock start to clock end (same century). */
int func_001D24A8(ClockTime start, ClockTime end) {
    int start_day;
    int end_day;
    char buf[0x30]; /* unused, but retail reserves the 0x30 bytes */

    start_day = func_001D2390(start);
    end_day = func_001D2390(end);
    if (start.year != end.year) {
        int year = start.year;

        if (year & 3) {
            end_day += (end.year - year) * 365;
        } else {
            end_day += (end.year - year) * 366;
        }
    }
    return (end.min - start.min) + ((end.hour - start.hour) + (end_day - start_day) * 24) * 60;
}

/* A play time (seconds, minutes, hours, and hundreds of hours in mday) as seconds. */
int func_001D2550(ClockTime play_time) {
    return play_time.sec + (play_time.min + (play_time.hour + play_time.mday * 99) * 60) * 60;
}

/* A play time as minutes. */
int func_001D2590(ClockTime play_time) {
    return play_time.min + (play_time.hour + play_time.mday * 99) * 60;
}

/* Read the console's real-time clock into `time`, its BCD fields made binary; gives sceCdReadClock's result. */
int func_001D25C8(ClockTime *time) {
    int result;

    result = sceCdReadClock(time);
    time->sec = BCD2DEC(time->sec);
    time->min = BCD2DEC(time->min);
    time->hour = BCD2DEC(time->hour);
    time->mday = BCD2DEC(time->mday);
    time->mon = BCD2DEC(time->mon);
    time->year = BCD2DEC(time->year);
    return result;
}
