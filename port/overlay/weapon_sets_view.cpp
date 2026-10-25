// The weapon sets mod's drawing (port/include/port/weapon_sets.h, docs/port/WEAPON_SETS.md), with Dear ImGui
// inside the overlay's frame, in the style of the extended stats mod's lines in the game's menus
// (port/overlay/stats_menus.cpp): its serif, the grey of the game's text, a dark shadow, the screen's fade.
//
// - On the equipment screen: a "2" after the row's name on Weapon 2 and Shield 2 (and a "1" on the game's
//   Weapon and Shield), and on those four rows set 2's names under the row's name.
// - In play, for a moment after a swap: what is in hand now.
//
// The places are the menus' 640 x 448 picture (MenuSprite_Draw draws a part at X - 1728, (Y - 1936) * 2), shown
// whole at 4:3 and, with widescreen, squeezed into the middle three quarters of the window.
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"

extern "C" {
#include "port/controls.h"
#include "port/host.h"
#include "port/platform.h"
#include "port/weapon_sets.h"
}

namespace {

const ImVec4 colour_text(0.86f, 0.85f, 0.81f, 1.0f);  // the game's menu text
const ImVec4 colour_label(0.66f, 0.66f, 0.64f, 1.0f);
const ImVec4 colour_name(0.93f, 0.92f, 0.89f, 1.0f);  // the rows' names, a little brighter

const unsigned long long MENU_FRESH = 3; // vblanks the equipment screen counts as up after it last ran
const unsigned long long SWAP_SHOWN = 100; // vblanks the swap's message stays, the last 30 fading

// The menus' serif, added by the extended stats mod (stats_menus_fonts); else ImGui's own
ImFont *serif() {
    static ImFont *found;
    static bool looked;
    if (!looked) {
        looked = true;
        ImFontAtlas *atlas = ImGui::GetIO().Fonts;
        for (int i = 1; i < atlas->Fonts.Size; i++) {
            const char *name = atlas->Fonts[i]->GetDebugName();
            if (name != nullptr && (std::strstr(name, "times") || std::strstr(name, "Serif")))
                found = atlas->Fonts[i];
        }
    }
    return found != nullptr ? found : ImGui::GetFont();
}

struct Frame {
    float x0, y0, sx, sy; // window = x0 + menu x * sx

    ImVec2 at(float x, float y) const { return ImVec2(std::floor(x0 + x * sx + 0.5f), std::floor(y0 + y * sy + 0.5f)); }
};

Frame frame_of(int x, int y, int width, int height) {
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float fx = scale.x > 0.0f ? scale.x : 1.0f, fy = scale.y > 0.0f ? scale.y : 1.0f;
    float px = (float)x / fx, py = (float)y / fy, pw = (float)width / fx, ph = (float)height / fy;
    if (pw < 32.0f || ph < 32.0f) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        px = vp->Pos.x, py = vp->Pos.y, pw = vp->Size.x, ph = vp->Size.y;
    }
    Frame f;
    if (port_widescreen()) {
        f.x0 = px + pw * 0.125f;
        f.sx = pw * 0.75f / 640.0f;
    } else {
        f.x0 = px;
        f.sx = pw / 640.0f;
    }
    f.y0 = py;
    f.sy = ph / 448.0f;
    return f;
}

ImU32 colour(const ImVec4 &c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

void text(ImDrawList *dl, float size, ImVec2 at, const ImVec4 &c, float alpha, const char *t) {
    float shadow = std::fmax(1.0f, std::floor(size / 14.0f));
    dl->AddText(serif(), size, ImVec2(at.x + shadow, at.y + shadow), colour(ImVec4(0, 0, 0, 0.85f), alpha), t);
    dl->AddText(serif(), size, at, colour(c, alpha), t);
}

float width_of(float size, const char *t) {
    return serif()->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x;
}

bool menu_up() {
    const PortWeaponSetsMenu &m = port_weapon_sets_menu;
    return m.vblank != 0 && port_vblank_count() - m.vblank <= MENU_FRESH;
}

bool swap_shown() {
    const PortWeaponSetsSwap &s = port_weapon_sets_swap;
    return s.vblank != 0 && port_vblank_count() - s.vblank < SWAP_SHOWN && !menu_up();
}

// The key's name for people ("R")
void key_name(char *out, size_t size) {
    std::snprintf(out, size, "R");
    for (int slot = 0; slot < BINDING_SLOTS; slot++) {
        int binding = port_controls_binding(port_controls_layout(), ACTION_WEAPON_SWAP, slot);
        if (binding != 0) {
            port_controls_binding_label(binding, out, size);
            return;
        }
    }
}

void equipment(ImDrawList *dl, const Frame &f) {
    const PortWeaponSetsMenu &m = port_weapon_sets_menu;
    if (m.group != 0 && m.group != 2)
        return;
    float alpha = std::fmin(1.0f, std::fmax(0.0f, m.alpha));
    if (alpha <= 0.01f)
        return;
    // the row's name: the word MenuDraw_Word draws, centred at x 137 (GS 1865), y 76 to 96 (GS 1974 to 1984)
    float size = 24.0f * f.sy;
    const char *number = m.row != 0 ? "2" : "1";
    ImVec2 at = f.at(137.0f + m.label_half_width + 7.0f, 73.0f);
    text(dl, size, at, colour_name, alpha, number);
    if (m.covered)
        return;
    // set 2, under the row's name: the left box (equip_para.dat), x 37 to 286, from y 104
    char line[2 * PORT_WEAPON_SETS_NAME + 32], key[64];
    if (m.empty)
        std::snprintf(line, sizeof(line), "Set 2: empty");
    else if (m.shield[0] != 0 && std::strcmp(m.shield, "None") != 0)
        std::snprintf(line, sizeof(line), "Set 2: %s, %s", m.weapon, m.shield);
    else
        std::snprintf(line, sizeof(line), "Set 2: %s", m.weapon);
    float small = 11.0f * f.sy;
    float room = (286.0f - 37.0f - 8.0f) * f.sx;
    while (small > 7.0f * f.sy && width_of(small, line) > room)
        small -= 0.5f;
    ImVec2 left = f.at(37.0f, 106.0f), right = f.at(286.0f, 106.0f);
    float x = std::floor((left.x + right.x - width_of(small, line)) * 0.5f);
    text(dl, small, ImVec2(x, left.y), colour_text, alpha, line);
    key_name(key, sizeof(key));
    char hint[96];
    if (m.row != 0)
        std::snprintf(hint, sizeof(hint), "Cross keeps it for set 2; %s swaps in play", key);
    else
        std::snprintf(hint, sizeof(hint), "%s swaps it with what is worn", key);
    float hs = small * 0.85f;
    x = std::floor((left.x + right.x - width_of(hs, hint)) * 0.5f);
    text(dl, hs, ImVec2(x, left.y + std::floor(small * 1.2f)), colour_label, alpha, hint);
}

void swap_message(ImDrawList *dl, const Frame &f) {
    const PortWeaponSetsSwap &s = port_weapon_sets_swap;
    unsigned long long age = port_vblank_count() - s.vblank;
    float alpha = age + 30 > SWAP_SHOWN ? (float)(SWAP_SHOWN - age) / 30.0f : 1.0f;
    float size = 16.0f * f.sy;
    float w = width_of(size, s.text);
    ImVec2 centre = f.at(320.0f, 300.0f);
    ImVec2 at(std::floor(centre.x - w * 0.5f), centre.y);
    dl->AddRectFilled(ImVec2(at.x - size * 0.7f, at.y - size * 0.3f), ImVec2(at.x + w + size * 0.7f, at.y + size * 1.3f),
                      colour(ImVec4(0, 0, 0, 0.45f), alpha), size * 0.3f);
    text(dl, size, at, colour_text, alpha, s.text);
}

} // namespace

extern "C" int weapon_sets_view_visible(void) {
    return port_weapon_sets.enabled && (menu_up() || swap_shown());
}

extern "C" void weapon_sets_view_draw(int x, int y, int width, int height) {
    if (!weapon_sets_view_visible())
        return;
    Frame f = frame_of(x, y, width, height);
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    if (menu_up())
        equipment(dl, f);
    else
        swap_message(dl, f);
}

extern "C" void weapon_sets_view_mods_section(void) {
    ImGui::SeparatorText("Weapon sets");
    bool on = port_weapon_sets.enabled != 0;
    if (ImGui::Checkbox("Weapon sets", &on))
        port_weapon_sets_set_enabled(on);
    char key[64];
    key_name(key, sizeof(key));
    ImGui::SetItemTooltip("A second weapon and shield, swapped with the ones in hand on one key (%s, or a\n"
                          "controller's right stick press; rebind it on the Controls page). Choose them on the\n"
                          "equipment screen: Weapon 2 and Shield 2 are above Weapon (press Up). Set 2 is kept\n"
                          "with each save. (--weapon-sets)",
                          key);
    if (port_weapon_sets.forced) {
        ImGui::SameLine();
        ImGui::TextDisabled("(on for this run: command line)");
    }
}
