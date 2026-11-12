# Changelog

## v1.0.1 - 2026-11-12

Fix: works alongside Fast Travel v1.3.

- v1.0.0 and Fast Travel v1.3 patched the same 7 spots in the game (controller input and screen drawing), so
  whichever mod loaded last turned the other's menu and hints off. v1.0.1 hooks the controller through the game's
  import table and the drawing through its callers instead, so it no longer touches anything Fast Travel or Hyrule
  Cheat Menu patches. Tested with all three installed: every menu works.
- No change to the map icons, hints or settings.

## v1.0.0 - 2026-11-12

First release.

- Map icons for missing Gold Skulltulas, Pieces of Heart, Big Poes and Silver Rupee rooms, on the minimap and the pause map.
- Settings menu on the pause map (L + R), one page per kind of collectible; saved to the SD card.
- Hints with step-by-step directions (right stick on the pause map).
- No game files included: the two map icon pictures are patched in memory as the game loads them.
