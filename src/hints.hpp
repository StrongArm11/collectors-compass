// Hints on the pause map (see hints.cpp): the right stick picks a collectible's icon, which blinks, and a panel
// at the bottom of the screen says what it is, where, what it takes and whether you have it.
#pragma once

#include "common.hpp"

namespace mm {

enum class HintStatus : u8 { Missing, NotYet, Collected, Unknown };

struct HintInfo {
    u8 cat = 0;                 // Kind: 0 Gold Skulltula, 1 Piece of Heart, 2 Big Poe, 3 Silver Rupees
    const char* id = "";
    const char* area = "";
    const char* desc = "";
    const char* need = "";      // e.g. "Adult \x84 Night \x84 Hookshot" ("" = nothing special)
    const char* why = "";       // for NotYet: what's missing now
    HintStatus status = HintStatus::Missing;
    const char* steps = "";     // how to get it: one step per line ("" = none)
    int tokens = 0, tokenTotal = 0;   // Silver Rupees: how many of the room's rupees are collected
    int position = 0, total = 0;   // "2 of 5" on this map
    bool top = false;           // the icon is low on the screen: put the panel at the top
};

// From input.cpp: the right stick was tilted (dx, dy: -1, 0 or 1; dy 1 = up) on the pause map.
void hintsNudge(int dx, int dy);
// For the icon watcher: the last tilt since it was asked (false if none).
bool hintsTakeNudge(int& dx, int& dy);

// Shows the hint panel for this icon, or hides it.
void hintShow(const HintInfo& info);
void hintHide();

}  // namespace mm
