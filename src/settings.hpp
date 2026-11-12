// The mod's options, changed in the in-game settings menu (menu.cpp) and kept in a file on the SD card (settings.cpp).
#pragma once

#include "common.hpp"

namespace mm {

#ifndef MM_DEFAULT_ONLY_NOW
#define MM_DEFAULT_ONLY_NOW 1
#endif
#ifndef MM_DEFAULT_SHOW_COLLECTED
#define MM_DEFAULT_SHOW_COLLECTED 0
#endif

// The kinds of collectible the mod marks on the maps; each has its own options.
enum Kind : u8 { kSkulltula = 0, kHeart = 1, kPoe = 2, kRupee = 3, kKindCount = 4 };
constexpr const char* kKindNames[kKindCount] = {"Gold Skulltulas", "Pieces of Heart", "Big Poes", "Silver Rupees"};
constexpr const char* kKindName1[kKindCount] = {"Gold Skulltula", "Piece of Heart", "Big Poe", "Silver Rupees"};

struct KindSettings {
    bool show = true;                                // icons for this kind on the minimap and the pause map
    bool onlyNow = MM_DEFAULT_ONLY_NOW;              // hide missing ones you can't get yet (age, night, a tool)
    bool showCollected = MM_DEFAULT_SHOW_COLLECTED;  // also show collected ones (faded icon)
};

struct Settings {
    KindSettings kind[kKindCount];
    bool hints = true;                               // pick icons on the pause map with the right stick for a hint
};

extern Settings g_settings;
extern std::atomic<u32> g_settingsVersion;   // bumped on every change, so the icons are worked out again

// Reads the saved settings (once; call when the game is running, e.g. on the first map frame).
void settingsLoad();
// Writes the current settings to the file.
void settingsSave();

}  // namespace mm
