/* KFIV: not in ICO. libcdvd.a: sceCdSeek (derived name). One function at 0x0022f3d0, the
 * member between cdvd005 and cdvd015. */
#include <eekernel.h>
#include <stdio.h>
#include <sifrpc.h>
#include <libcdvd.h>
#include <libcdvd_internal.h>

/* volatile here; cdvd000 defines it plain and releases it with plain stores */
extern volatile int _sceCd_c_cb_sem;

int sceCdSeek(int lsn)
{
    int *sd = _sceCd_ncmdsdata;

    if (sceCdNcmdDiskReady() == 6) {
        return 0;
    }
    if (_sceCd_ncmd_prechk(9) == 0) {
        return 0;
    }
    *sd = lsn;
    sceSifWriteBackDCache(sd, 4);
    sceCdCbfunc_num = 4;
    _sceCd_c_cb_sem = 1;
    if (sceSifCallRpc(&_sceCd_cd_ncmd, 5, 1, sd, 4, 0, 0, _sceCd_cd_callback, &sceCdCbfunc_num) <
        0) {
        sceCdCbfunc_num = 0;
        _sceCd_c_cb_sem = 0;
        SignalSema(*(volatile int *)&_sceCd_ncmd_semid);
        return 0;
    }
    return 1;
}
