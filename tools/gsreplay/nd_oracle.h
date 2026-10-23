/* kf4-gsreplay --nd-oracle (nd_oracle.c): native draw's vertex oracle */
#ifndef KF4_GSREPLAY_ND_ORACLE_H
#define KF4_GSREPLAY_ND_ORACLE_H
#include <stddef.h>
#include <stdint.h>

/* vu_data: VU1's data memory; limit: the differences shown at most */
void nd_oracle_begin(const uint8_t *vu_data, int limit);
int nd_oracle_on(void);
/* a VU1 run starts: the program it runs (an MSCNT's: the program before), whether an MSCAL, its TOP */
void nd_oracle_vu1_run(uint32_t program, int mscal, uint32_t top);
/* a packet for the GS (vif1.c, vu1.c) */
void nd_oracle_gif(int path, const void *data, size_t bytes);
/* the last vertices run and compared, and the report */
void nd_oracle_end(void);
#endif
