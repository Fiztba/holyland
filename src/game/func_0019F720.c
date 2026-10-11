#include "kf_file.h"

extern char D_0043EE18[];
extern char D_004FF558[];
extern char D_004FF958[];

int func_0019F720(void) {
    int temp_v0;

    temp_v0 = func_00102238(D_0043EE18, 0, 1);
    func_001025C0(temp_v0, D_004FF558, 0x400);
    func_001025C0(temp_v0, D_004FF958, 0x800);
    func_001024B0(temp_v0);
    return 1;
}
