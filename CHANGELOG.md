# Changelog

## v1.0.2 - 2026-11-12

- The House of Skulltula's Piece of Heart (H-KAK4) now stays hidden until you have 50 Gold Skulltula tokens.
- Big Poe hints: the Poe Collector gives the bottle for five Big Poes in this version, not ten.
- Zora's River Pieces of Heart (H-ZRV1, H-ZRV2): corrected the directions. The pillar piece is easiest with the
  Boomerang; there are no vines to climb with the Cucco.
- Collectibles now also need what it takes to get into their area or dungeon, for "only what I can get now" and
  the hint panel. For example: the Hookshot for Death Mountain Crater (adult), Zelda's Lullaby (or the Silver Scale as
  a child) for Zora's Domain, Zora's Fountain, Jabu-Jabu's Belly and the Ice Cavern, the Song of Storms for the Bottom
  of the Well, the Nocturne of Shadow and Hover Boots for the Shadow Temple, the Requiem of Spirit for the child side
  of the Spirit Temple, the Goron Tunic for the Fire Temple and the Zora Tunic for the Water Temple. The hint steps
  say what getting there needs; a long "Needs" line now gets smaller type so it fits.

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
