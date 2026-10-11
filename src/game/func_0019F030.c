#include "kf_file.h"

extern void func_00102228(void);
extern int D_00446858;

typedef struct {
    char pad[0x24];
    int fd;
} Job;

int func_0019F030(Job *arg0) {
    if (func_00102790(arg0->fd, 1) != 0) {
        return -1;
    }
    func_001024B0(arg0->fd);
    func_00102228();
    arg0->fd = -1;
    D_00446858 += 1;
    return 0;
}
