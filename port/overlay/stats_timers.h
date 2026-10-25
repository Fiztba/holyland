// The extended stats mod's timers on screen, by the minimap (port/overlay/stats_timers.cpp), for
// port/overlay/stats_panel.cpp
#ifndef PORT_OVERLAY_STATS_TIMERS_H
#define PORT_OVERLAY_STATS_TIMERS_H

extern "C" {
#include "port/stats.h"
}

// once a vblank (port_stats_overlay_update): what runs, each one's share left, and the list's fade
void stats_timers_step(const PortStatsSnapshot &s);
// the list is shown or fading out: the overlay draws this vblank
bool stats_timers_wanted();
// draws it, inside the overlay's ImGui frame; x, y, width, height: the game's picture in the window
void stats_timers_draw(int x, int y, int width, int height);

#endif
