// The extended stats mod's timers on screen (port/include/port/stats.h, docs/port/EXTENDED_STATS.md): while
// playing, a small list of the ailments running and the timed effects of spells and items, each with its
// seconds left and a thin bar of the time left, by the automap's minimap: above it when it is in a bottom
// corner, below it in a top corner, lined up with its edge and as wide as it. With the minimap off (or the
// map mod off) the list takes the minimap's place in its corner. Hidden in the menus (their lines show the
// same), it fades out as the last one ends.
//
// Drawn with Dear ImGui, no window, in the serif of the menus' lines and the minimap's frame.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

#include "automap_view.h"
#include "stats_menus.h"
#include "stats_timers.h"

extern "C" {
#include "port/platform.h"
#include "port/stats.h"
}

namespace {

const char *const ailment_names[6] = {"Poison", "Paralysis", "Darkness", "Curse", "Slow", "Magic seal"};
// each ailment in the colour of what it does: poison's green tint, the curse's purple, slow's white
// (Player_UpdateScreenTints), the dark, the held body, the sealed magic
const ImVec4 ailment_colours[6] = {
    ImVec4(0.56f, 0.86f, 0.46f, 1.0f), ImVec4(0.96f, 0.66f, 0.36f, 1.0f), ImVec4(0.64f, 0.64f, 0.76f, 1.0f),
    ImVec4(0.80f, 0.56f, 0.92f, 1.0f), ImVec4(0.90f, 0.90f, 0.86f, 1.0f), ImVec4(0.52f, 0.72f, 0.96f, 1.0f),
};
// the timed effects (good ones): the grey of the menus' text, with the minimap frame's gold for the bar
const ImVec4 colour_effect(0.86f, 0.85f, 0.81f, 1.0f);
const ImVec4 colour_effect_bar(0.86f, 0.74f, 0.46f, 1.0f);
const ImVec4 colour_seconds(0.86f, 0.85f, 0.81f, 1.0f);

const int MOST_ROWS = 8;

struct Row {
    std::string key;  // what it is, the same while it runs
    std::string name;
    int left;         // game frames left (an ailment's taken at the speed it wears off)
    float fraction;   // of the time it had when it started (or was last made longer)
    ImVec4 colour, bar;
};

struct Peak {
    int last, most;
};

std::vector<Row> rows;           // drawn: what runs now, or what ran last while it fades out
std::map<std::string, Peak> peaks; // each timer's frames when last started or made longer
float alpha;                     // the list's fade, stepped a vblank at a time
bool active;                     // something runs and the list may show

// a timer's share left of the most it had since it started: a timer that rose (cast again, a new hit)
// starts again from there
float share(const std::string &key, int frames, std::map<std::string, Peak> &next) {
    auto it = peaks.find(key);
    Peak p = {frames, frames};
    if (it != peaks.end() && frames <= it->second.last)
        p.most = std::max(it->second.most, frames);
    next[key] = p;
    return p.most > 0 ? (float)frames / (float)p.most : 0.0f;
}

ImU32 colour(const ImVec4 &c, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

ImFont *font() {
    ImFont *f = stats_menus_font();
    return f != nullptr ? f : ImGui::GetFont();
}

float text_width(float size, const char *text) {
    return font()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void text(ImDrawList *dl, float size, ImVec2 at, const ImVec4 &c, float a, const char *t) {
    float shadow = std::max(1.0f, std::floor(size / 14.0f));
    dl->AddText(font(), size, ImVec2(at.x + shadow, at.y + shadow), colour(ImVec4(0, 0, 0, 0.85f), a), t);
    dl->AddText(font(), size, at, colour(c, a), t);
}

} // namespace

void stats_timers_step(const PortStatsSnapshot &s) {
    std::vector<Row> now;
    std::map<std::string, Peak> next;
    bool on = port_stats_options.on && port_stats_options.timers && s.valid;

    if (on) {
        int thirds = s.ailment_wear_thirds > 0 ? s.ailment_wear_thirds : 3;
        for (int i = 0; i < 6; i++) {
            if (s.ailment_frames[i] <= 0)
                continue;
            Row r;
            r.key = ailment_names[i];
            r.name = ailment_names[i];
            // timed_flags wear by ailment_wear_thirds / 3 a frame (Demon Armor, Purity Amulet)
            r.left = s.ailment_frames[i] * 3 / thirds;
            r.fraction = share(r.key, s.ailment_frames[i], next);
            r.colour = r.bar = ailment_colours[i];
            now.push_back(r);
        }
        for (int i = 0; i < s.timer_count; i++) {
            const PortStatsTimer &t = s.timers[i];
            Row r;
            r.key = std::string("+") + t.text;
            r.name = t.brief[0] ? t.brief : t.text;
            r.left = t.frames;
            r.fraction = share(r.key, t.frames, next);
            r.colour = colour_effect;
            r.bar = colour_effect_bar;
            now.push_back(r);
        }
    }
    peaks.swap(next);
    if (now.size() > (size_t)MOST_ROWS)
        now.resize(MOST_ROWS);
    active = on && s.in_play && !now.empty();
    if (!now.empty()) {
        rows = now;
    } else if (!rows.empty() && on && s.in_play) {
        // the last one ended: it stays at nothing left while the list fades out
        for (Row &r : rows) {
            r.left = 0;
            r.fraction = 0.0f;
        }
    }
    // in at once (a fifth of a second), out over half a second; a vblank at a time, as the menus' lines
    if (active)
        alpha = std::min(1.0f, alpha + 0.084f);
    else
        alpha = std::max(0.0f, alpha - (s.in_play ? 0.034f : 0.2f));
    if (alpha <= 0.0f && !active)
        rows.clear();
}

bool stats_timers_wanted() {
    return active || alpha > 0.0f;
}

void stats_timers_draw(int x, int y, int width, int height) {
    if (alpha <= 0.01f || rows.empty() || !port_stats_options.on || !port_stats_options.timers)
        return;
    float box[4], opacity;
    int corner;
    bool map_shown = automap_view_minimap_place(x, y, width, height, box, &corner, &opacity);
    bool right = corner == 0 || corner == 1, bottom = corner == 1 || corner == 3;

    // the picture in ImGui's coordinates (as the minimap's)
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float sx = scale.x > 0.0f ? scale.x : 1.0f, sy = scale.y > 0.0f ? scale.y : 1.0f;
    float g0y = (float)y / sy, gh = (float)height / sy;
    if (gh < 32.0f) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        g0y = vp->WorkPos.y, gh = vp->WorkSize.y;
    }

    // the menus' lines' size at about the game's row: 24 pixels at 1080 rows, 33 at 1440
    float size = std::max(13.0f, std::floor(gh / 44.0f + 0.5f));
    float side = box[2] - box[0];
    float pad = std::floor(size * 0.45f), gap = std::floor(size * 0.6f);
    // as wide as the minimap; smaller text (to three quarters) before wider than it
    auto widest = [&](float sz) {
        float w = 0.0f;
        char value[32];
        for (const Row &r : rows) {
            std::snprintf(value, sizeof(value), "%d s", (r.left + PORT_STATS_GAME_FPS - 1) / PORT_STATS_GAME_FPS);
            w = std::max(w, text_width(sz, r.name.c_str()) + sz * 0.6f + text_width(sz, value));
        }
        return w + 2.0f * pad;
    };
    float w = side;
    while (widest(size) > w && size > 13.0f && size * 4.0f > std::floor(gh / 44.0f + 0.5f) * 3.0f)
        size -= 0.5f;
    w = std::max(w, std::ceil(widest(size)));
    pad = std::floor(size * 0.45f);
    float bar_h = std::max(2.0f, std::floor(size * 0.12f));
    float pitch = std::floor(size * 1.12f) + bar_h + std::floor(size * 0.28f);
    float h = pitch * (float)rows.size() + 2.0f * pad - std::floor(size * 0.28f);

    float x0 = right ? box[2] - w : box[0];
    float top;
    if (bottom)
        top = (map_shown ? box[1] - gap : box[3]) - h;
    else
        top = map_shown ? box[3] + gap : box[1];
    // clear of the HUD's orb at the top left: its ring reaches about 22% of the picture's height at 4:3 and
    // 24% with the widescreen HUD (the minimap's own place there, 22%, is under it only when it is drawn)
    if (!right && !bottom)
        top = std::max(top, g0y + gh * 0.26f);
    // clear of the frame-rate counter (F3) at the top right
    if (right && !bottom && port_video_counter_shown()) {
        int cell = std::max(height / 360, 2), margin = std::max(height * 3 / 100, cell * 2);
        top = std::max(top, g0y + (float)(margin + port_video_counter_height(height)) / sy + gap);
    }
    top = std::floor(top);
    x0 = std::floor(x0);

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    float round = side * 0.04f;
    // the minimap's look: its dark square and its gold frame
    dl->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + w, top + h), colour(ImVec4(10 / 255.0f, 9 / 255.0f, 8 / 255.0f, 0.75f * opacity / 0.85f), alpha),
                      round);
    dl->AddRect(ImVec2(x0, top), ImVec2(x0 + w, top + h), colour(ImVec4(200 / 255.0f, 180 / 255.0f, 130 / 255.0f, 0.8f), alpha), round, 0,
                1.5f);
    float ty = top + pad - std::floor(size * 0.05f);
    char value[32];
    for (const Row &r : rows) {
        std::snprintf(value, sizeof(value), "%d s", (r.left + PORT_STATS_GAME_FPS - 1) / PORT_STATS_GAME_FPS);
        float l = x0 + pad, rt = x0 + w - pad;
        text(dl, size, ImVec2(l, ty), r.colour, alpha, r.name.c_str());
        text(dl, size, ImVec2(std::floor(rt - text_width(size, value)), ty), colour_seconds, alpha, value);
        float by = std::floor(ty + size * 1.12f);
        float f = std::min(1.0f, std::max(0.0f, r.fraction));
        dl->AddRectFilled(ImVec2(l, by), ImVec2(rt, by + bar_h), colour(ImVec4(1, 1, 1, 0.12f), alpha));
        dl->AddRectFilled(ImVec2(l, by), ImVec2(l + std::floor((rt - l) * f), by + bar_h),
                          colour(ImVec4(r.bar.x, r.bar.y, r.bar.z, 0.85f), alpha));
        ty += pitch;
    }
}
