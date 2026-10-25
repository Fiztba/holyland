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

/* program 0x280's (nd_oracle_280.c): the same, called by nd_oracle.c's; and the blocks VIF1 is sent
   (gsreplay.c, before each is fed; program: the VU1 program last started) */
void nd280_oracle_begin(const uint8_t *vu_data, int limit);
void nd280_oracle_vu1_run(uint32_t program, int mscal, uint32_t top);
void nd280_oracle_gif(int path, const void *data, size_t bytes);
void nd280_oracle_end(void);
void nd280_oracle_vifdata(const uint32_t *words, size_t count, uint32_t program);

/* program 0x700's, the effects (nd_oracle_700.c): the same */
void nd700_oracle_begin(const uint8_t *vu_data, int limit);
void nd700_oracle_vu1_run(uint32_t program, int mscal, uint32_t top);
void nd700_oracle_gif(int path, const void *data, size_t bytes);
void nd700_oracle_end(void);
void nd700_oracle_vifdata(const uint32_t *words, size_t count, uint32_t program);
#endif
