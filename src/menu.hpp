// The mod's settings menu on the pause map (see menu.cpp) and the controller input it needs (input.cpp).
#pragma once

#include "common.hpp"

namespace mm {

// Controller buttons (nn::hid NpadButton bits); the stick directions are worked out from the sticks.
constexpr u64 kBtnA = 1u << 0, kBtnB = 1u << 1, kBtnX = 1u << 2, kBtnY = 1u << 3, kBtnL = 1u << 6, kBtnR = 1u << 7,
              kBtnPlus = 1u << 10, kBtnMinus = 1u << 11, kBtnLeft = 1u << 12, kBtnUp = 1u << 13, kBtnRight = 1u << 14,
              kBtnDown = 1u << 15;
constexpr u64 kMenuChord = kBtnL | kBtnR;   // pressed together on the pause map: open or close the menu

// The system tick, read like nn::os::GetSystemTick does (cntpct_el0; Lichii's CPU emulation has no cntvct_el0), and
// its rate as nn::os::GetSystemTickFrequency gives it on this console (31.25 MHz; cntfrq_el0 isn't read).
inline u64 ticksNow() { u64 v; asm volatile("mrs %0, cntpct_el0" : "=r"(v)); return v; }
constexpr u64 tickRate() { return 31250000; }

// From the icon watcher, each frame a map's icons are updated: pauseMap is true on the pause map.
void menuNoteMap(bool pauseMap);
// The pause map is on screen (seen in the last fraction of a second).
bool menuMapOpen();

bool menuIsOpen();
void menuOpen();
void menuClose();
// Newly pressed buttons while the menu is open (moves, changes, closes).
void menuInput(u64 pressed);

// Installs the controller hooks; false if the patch is missing.
bool installInput();
void inputTick();   // every frame: hooks the controller imports once the game has bound them

}  // namespace mm
