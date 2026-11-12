// The settings menu: a panel over the pause map, opened with L + R (input.cpp hands it the button presses and keeps
// them from the game while it's open). Up/Down choose, A or Left/Right change, B closes. Changes apply at once
// (the icon watcher works the icons out again) and are saved to the SD card when the menu closes.
#include "menu.hpp"

#include "overlay.hpp"
#include "settings.hpp"

namespace mm {
namespace {

std::atomic<u64> s_pauseMapTick{0};
std::atomic<bool> s_open{false};
std::atomic<u32> s_version{1};
SpinLock s_lock;
int s_cursor = 0;
bool s_changed = false;   // a setting changed since the menu opened: saved when it closes

// Two levels: the main page lists the four kinds of collectible (each opens its own page), the hints switch and
// Close; a kind's page has its three switches and Back.
int s_page = -1;          // -1 the main page, else the kind (Kind) whose page is open

struct Row { const char* label; int kind; bool KindSettings::*value; bool Settings::*global; const char* help; };
constexpr int kMain = -1;
constexpr Row kMainRows[] = {
    {"Gold Skulltulas", kSkulltula, nullptr, nullptr, "Gold Skulltula icons: show them, and which ones."},
    {"Pieces of Heart", kHeart, nullptr, nullptr, "Piece of Heart icons: show them, and which ones."},
    {"Big Poes", kPoe, nullptr, nullptr, "The ten Big Poes in Hyrule Field (adult), with the game's Poe icon."},
    {"Silver Rupees", kRupee, nullptr, nullptr, "Silver Rupee rooms in the dungeons: one icon per room, until all five are collected."},
    {"Hints on the pause map", kMain, nullptr, &Settings::hints,
     "Tilt the right stick on the pause map to pick an icon and see how to get it."},
    {"Close", kMain, nullptr, nullptr, "Back to the map. Press L + R together on the map to open this menu again."},
};
constexpr Row kKindRows[] = {
    {"Show on the maps", kMain, &KindSettings::show, nullptr, "Icons on the minimap and the pause map."},
    {"Only what I can get now", kMain, &KindSettings::onlyNow, nullptr,
     "Hide missing ones Link can't get yet: the wrong age, night only, or an item you don't have."},
    {"Show collected (faded)", kMain, &KindSettings::showCollected, nullptr, "Also show the ones you already have, as faded icons."},
    {"Back", kMain, nullptr, nullptr, "Back to the list of collectibles (B does the same)."},
};
constexpr int kMainCount = sizeof(kMainRows) / sizeof(kMainRows[0]);
constexpr int kKindCountRows = sizeof(kKindRows) / sizeof(kKindRows[0]);

const Row* rows(int page, int& count) {
    if (page == kMain) { count = kMainCount; return kMainRows; }
    count = kKindCountRows;
    return kKindRows;
}

// A row's switch on the given page, or nullptr for rows that open a page or close.
bool* rowValue(Settings& s, int page, const Row& r) {
    if (r.global != nullptr) return &(s.*(r.global));
    if (r.value != nullptr && page >= 0) return &(s.kind[page].*(r.value));
    return nullptr;
}

// ---- look (1080p units) ----
constexpr int kWidth = 760, kHeight = 572;
constexpr float kPad = 36.0f, kRowTop = 100.0f, kRowHeight = 52.0f;
constexpr u32 kBg = 0x15171c, kBorder = 0xb8964b, kTitle = 0xf2e7c9, kText = 0xe6e6e6, kSelectedBg = 0x272b35,
              kOn = 0x7fd892, kOff = 0x8b9099, kHelp = 0xb4b9c2, kFooter = 0x969ca6, kDivider = 0x3a3f4a;

// Word-wraps text into lines that fit width; returns how many were drawn.
int drawWrapped(Canvas& c, float x, float baseline, float lineHeight, float width, float size, const char* text, u32 rgb,
                u32 bg, int maxLines) {
    char line[128];
    int lines = 0;
    const char* p = text;
    while (*p != 0 && lines < maxLines) {
        int n = 0, lastSpace = -1;
        const char* q = p;
        while (*q != 0 && n < static_cast<int>(sizeof(line)) - 1) {
            line[n] = *q; line[n + 1] = 0;
            if (Canvas::textWidth(line, size) > width) break;
            if (*q == ' ') lastSpace = n;
            ++n; ++q;
        }
        if (*q != 0 && lastSpace > 0) { n = lastSpace; q = p + lastSpace + 1; }
        line[n] = 0;
        c.text(x, baseline + lineHeight * static_cast<float>(lines), size, line, rgb, bg);
        ++lines;
        p = q;
        while (*p == ' ') ++p;
    }
    return lines;
}

void paintMenu(Canvas& c) {
    Settings now;
    int cursor, page;
    {
        LockGuard guard(s_lock);
        now = g_settings;
        cursor = s_cursor;
        page = s_page;
    }
    int count = 0;
    const Row* list = rows(page, count);
    const float w = static_cast<float>(kWidth), h = static_cast<float>(kHeight);
    c.fill(0, 0, w, h, kBg);
    c.frame(0, 0, w, h, 3, kBorder);
    c.frame(7, 7, w - 14, h - 14, 1, kDivider);
    if (page == kMain) {
        c.text(kPad, 62, 36, "Collector's Compass", kTitle, kBg);
    } else {
        const float x = c.text(kPad, 62, 26, "Collector's Compass \x83 ", kFooter, kBg);
        c.text(kPad + x, 62, 36, kKindNames[page], kTitle, kBg);
    }
    const char* chord = "L + R";
    c.text(w - kPad - Canvas::textWidth(chord, 22), 60, 22, chord, kFooter, kBg);
    c.fill(kPad, 84, w - 2 * kPad, 2, kBorder);
    for (int i = 0; i < count; ++i) {
        const float top = kRowTop + kRowHeight * static_cast<float>(i);
        const bool selected = i == cursor;
        const u32 bg = selected ? kSelectedBg : kBg;
        if (selected) {
            c.fill(18, top, w - 36, kRowHeight - 4, kSelectedBg);
            c.fill(18, top, 5, kRowHeight - 4, kBorder);
        }
        const float baseline = top + 35;
        c.text(kPad + 6, baseline, 27, list[i].label, selected ? 0xffffff : kText, bg);
        const bool* v = rowValue(now, page, list[i]);
        if (v != nullptr) {
            const char* t = *v ? "On" : "Off";
            c.text(w - kPad - 6 - Canvas::textWidth(t, 27), baseline, 27, t, *v ? kOn : kOff, bg);
        } else if (page == kMain && list[i].kind >= 0) {   // a kind: a short summary, and the arrow into its page
            const KindSettings& k = now.kind[list[i].kind];
            const char* t = !k.show ? "Off  \x83" : k.onlyNow ? (k.showCollected ? "Can get + got  \x83" : "Can get now  \x83")
                                                              : (k.showCollected ? "All  \x83" : "Missing  \x83");
            c.text(w - kPad - 6 - Canvas::textWidth(t, 23), baseline, 23, t, k.show ? kOn : kOff, bg);
        }
    }
    const float helpTop = kRowTop + kRowHeight * static_cast<float>(count) + 10;
    c.fill(kPad, helpTop, w - 2 * kPad, 1, kDivider);
    drawWrapped(c, kPad + 6, helpTop + 34, 30, w - 2 * kPad - 12, 22, list[cursor].help, kHelp, kBg, 2);
    c.fill(kPad, h - 62, w - 2 * kPad, 1, kDivider);
    c.text(kPad + 6, h - 26, 22, page == kMain ? "\x80\x81 Choose      A  Change / open      B  Close"
                                               : "\x80\x81 Choose      A  Change      B  Back", kFooter, kBg);
}

void show() {
    setPanel(true, kWidth, kHeight, -1, -1, s_version.fetch_add(1) + 1, &paintMenu);
}

}  // namespace

void menuNoteMap(bool pauseMap) {
    if (pauseMap) s_pauseMapTick.store(ticksNow(), std::memory_order_release);
}

bool menuMapOpen() {
    const u64 seen = s_pauseMapTick.load(std::memory_order_acquire);
    return seen != 0 && ticksNow() - seen < tickRate() / 4;
}

bool menuIsOpen() { return s_open.load(std::memory_order_acquire); }

void menuOpen() {
    if (s_open.exchange(true)) return;
    log("menu: opened");
    show();
}

void menuClose() {
    if (!s_open.exchange(false)) return;
    setPanel(false);
    {
        LockGuard guard(s_lock);
        s_page = kMain; s_cursor = 0;
    }
    if (s_changed) { s_changed = false; settingsSave(); }
    const Settings& s = g_settings;
    log("menu: closed (Skulltulas %d%d%d, hearts %d%d%d, Poes %d%d%d, rupees %d%d%d [show, only now, collected], hints %s)",
        s.kind[0].show, s.kind[0].onlyNow, s.kind[0].showCollected, s.kind[1].show, s.kind[1].onlyNow, s.kind[1].showCollected,
        s.kind[2].show, s.kind[2].onlyNow, s.kind[2].showCollected, s.kind[3].show, s.kind[3].onlyNow, s.kind[3].showCollected,
        s.hints ? "on" : "off");
}

void menuInput(u64 pressed) {
    if (pressed == 0) return;
    bool changed = false;
    int page, count = 0;
    {
        LockGuard guard(s_lock);
        if (pressed & kBtnB) {
            if (s_page == kMain) { changed = false; }
            else { s_cursor = s_page; s_page = kMain; changed = true; }
        }
    }
    if ((pressed & kBtnB) && !changed) { menuClose(); return; }
    if (changed) { show(); return; }
    {
        LockGuard guard(s_lock);
        rows(s_page, count);
        if (pressed & kBtnUp) { s_cursor = (s_cursor + count - 1) % count; changed = true; }
        if (pressed & kBtnDown) { s_cursor = (s_cursor + 1) % count; changed = true; }
        page = s_page;
    }
    const Row& row = rows(page, count)[s_cursor];
    if (pressed & (kBtnA | kBtnLeft | kBtnRight)) {
        bool* v = rowValue(g_settings, page, row);
        if (v != nullptr) {
            {
                LockGuard guard(s_lock);
                *v = !*v;
            }
            g_settingsVersion.fetch_add(1, std::memory_order_release);
            s_changed = true;
            log("menu: %s%s%s %s", page >= 0 ? kKindNames[page] : "", page >= 0 ? ": " : "", row.label, *v ? "on" : "off");
            changed = true;
        } else if (page == kMain && row.kind >= 0) {
            if (pressed & (kBtnA | kBtnRight)) {
                LockGuard guard(s_lock);
                s_page = row.kind; s_cursor = 0;
                changed = true;
            }
        } else if (pressed & kBtnA) {
            if (page == kMain) { menuClose(); return; }   // Close
            LockGuard guard(s_lock);                      // Back
            s_cursor = s_page; s_page = kMain;
            changed = true;
        }
    }
    if (changed) show();
}

}  // namespace mm
