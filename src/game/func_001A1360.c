#include "common.h"

int func_00102238(int, int, int);
int func_001025C0(int, void *, int);
int func_001024B0(int);
int func_001CD0E0(int, int, int, int, int, int, int);

/* Loads two files into the buffers described by the table and registers them; returns 0 if either fails to open. */
int func_001A1360(char *name_a, char *name_b, s32 *table, int slot, int extra) {
    int handle;

    handle = func_00102238((int)name_a, FALSE, TRUE);
    if (handle < 0) {
        func_001024B0(handle);
        return 0;
    }
    func_001025C0(handle, (void *)table[0], table[3]);
    func_001024B0(handle);
    handle = func_00102238((int)name_b, FALSE, TRUE);
    if (handle < 0) {
        func_001024B0(handle);
        return 0;
    }
    func_001025C0(handle, (void *)table[1], table[4]);
    func_001024B0(handle);
    return func_001CD0E0(slot, table[1], table[4], table[0], table[3], 0, extra);
}
