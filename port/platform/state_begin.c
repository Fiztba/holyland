/* The start of the program's state image (port/platform/savestate.c). The build links this object
   first, then the game's objects and ico-audio's, then port/platform/state_end.c: the linker lays
   each object's data and its zeroed data out in that order, so everything those objects keep in
   globals and statics lies between these markers and the end's. */
char port_state_data_begin[16] __attribute__((aligned(16))) = {1};
char port_state_bss_begin[16] __attribute__((aligned(16)));
