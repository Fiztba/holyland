// The extended stats mod's lines in the game's menus (port/overlay/stats_menus.cpp), for
// port/overlay/stats_panel.cpp
#ifndef PORT_OVERLAY_STATS_MENUS_H
#define PORT_OVERLAY_STATS_MENUS_H

extern "C" {
#include "port/stats.h"
}

void stats_menus_fonts();                           // adds the menus' font, once at start
struct ImFont;
ImFont *stats_menus_font();                         // that font (nullptr: none found, ImGui's own is used)
bool stats_menus_wanted(const PortStatsSnapshot &s); // a screen of the menu is up and its lines are on
// once a vblank (port_stats_overlay_update): their fade follows the screen's
void stats_menus_step(const PortStatsSnapshot &s);
// draws them, inside the overlay's ImGui frame; x, y, width, height: the game's picture in the window
void stats_menus_draw(const PortStatsSnapshot &s, int x, int y, int width, int height);

#endif
