// Drawing on top of the game's HUD (see overlay.cpp): one solid panel at a time (the settings menu, or a hint).
#pragma once

#include "common.hpp"

namespace mm {

// The panel's pixels while it is being painted. Coordinates and sizes are in 1080p units (the canvas scales them to
// the screen); colours are 0xRRGGBB (sRGB).
struct Canvas {
    u32* pixels = nullptr;
    int rowWords = 0, width = 0, height = 0;   // in screen pixels
    float scale = 1.0f;                        // screen pixels per 1080p unit
    int format = 0;
    const u32 (*lut)[256] = nullptr;           // for float screens: 8-bit sRGB -> packed channel

    void fill(float x, float y, float w, float h, u32 rgb);
    void frame(float x, float y, float w, float h, float thickness, u32 rgb);
    // Draws text with its baseline at y, over a known background colour (text is blended with it). Returns the
    // advance in 1080p units. Bytes 0x80.. are symbols: up, down, left, right arrows, bullet, check mark.
    float text(float x, float baseline, float size, const char* s, u32 rgb, u32 background);
    static float textWidth(const char* s, float size);
    u32 pack(u32 rgb) const;
};

using PaintFn = void (*)(Canvas& canvas);

// Shows the panel (width x height in 1080p units; x, y its top left, or -1 to centre it), painted by paint()
// whenever version changes; or hides it. Safe from any thread.
void setPanel(bool visible, int width = 0, int height = 0, int x = -1, int y = -1, u32 version = 0, PaintFn paint = nullptr);

// Installs the graphics-table and layer-draw hooks; false if the patch or the game's code is not as expected.
bool installOverlay();

}  // namespace mm
