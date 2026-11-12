// What the mod reads from the running game: flags (by name hash), Link's age, the time of day and the current area.
// Offsets are for build E0F3CCFA... (the same ones the external overlay uses, read here in-process).
#pragma once

#include "common.hpp"

namespace mm {

struct GameState {
    bool valid = false;
    bool child = false;
    int hour = -1, minute = 0;       // -1: unknown
    char banc[48] = {};              // area name, e.g. "KokiriForest" ("" if unknown)
    bool night() const { return hour >= 0 && (hour >= 18 || hour < 6); }
};

// Refreshes age, time and area (cheap; call at most a few times per second).
void readGameState(GameState& out);

// A game flag by its 32-bit name hash: 1 set, 0 clear, -1 not found.
int gameBool(u32 hash);

// A 64-bit save key (Gold Skulltulas, some hearts): true if the save has it and it is set.
bool gameKey(u64 key);

}  // namespace mm
