/* The sound loads waiting to run, one at a time (sound/sound.h's SoundLoadQueue). */
#include "sound/sound.h"

/* 0x38A130 the queue */
SoundLoadQueue sound_load_queue = { 0 };
