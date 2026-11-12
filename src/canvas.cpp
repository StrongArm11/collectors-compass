// Painting the overlay panel: filled rectangles and text from the built-in bitmap font (build/font.inc), scaled
// to the screen with area sampling so it stays smooth at any size.
#include "overlay.hpp"

#include "font.inc"

namespace mm {
namespace {

constexpr int kFormatR11G11B10F = 0x3F;

int floorInt(float v) { const int i = static_cast<int>(v); return (static_cast<float>(i) > v) ? i - 1 : i; }
int ceilInt(float v) { const int i = static_cast<int>(v); return (static_cast<float>(i) < v) ? i + 1 : i; }
int roundInt(float v) { return floorInt(v + 0.5f); }

const font::Glyph* glyphFor(unsigned char c) {
    int index = -1;
    if (c >= 32 && c < 127) index = c - 32;
    else if (c >= 0x80 && c < 0x80 + (font::kCount - 95)) index = 95 + (c - 0x80);
    return index >= 0 ? &font::kGlyphs[index] : nullptr;
}

// Coverage at a point in the glyph's own pixels (bilinear; 0 outside).
float coverage(const font::Glyph& g, float sx, float sy) {
    sx -= 0.5f; sy -= 0.5f;
    const int x0 = floorInt(sx), y0 = floorInt(sy);
    const float fx = sx - static_cast<float>(x0), fy = sy - static_cast<float>(y0);
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= g.width || y >= g.height) return 0.0f;
        return static_cast<float>(font::kCoverage[g.offset + static_cast<u32>(y) * g.width + static_cast<u32>(x)]);
    };
    const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return (top + (bottom - top) * fy) * (1.0f / 255.0f);
}

u32 mix(u32 bg, u32 fg, float t) {
    u32 out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const float b = static_cast<float>((bg >> shift) & 0xFF), f = static_cast<float>((fg >> shift) & 0xFF);
        out |= static_cast<u32>(roundInt(b + (f - b) * t)) << shift;
    }
    return out;
}

}  // namespace

u32 Canvas::pack(u32 rgb) const {
    const u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    if (format == kFormatR11G11B10F && lut != nullptr) return lut[0][r] | lut[1][g] | lut[2][b];
    return 0xFF000000u | b << 16 | g << 8 | r;
}

void Canvas::fill(float x, float y, float w, float h, u32 rgb) {
    int x0 = roundInt(x * scale), y0 = roundInt(y * scale), x1 = roundInt((x + w) * scale), y1 = roundInt((y + h) * scale);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    const u32 word = pack(rgb);
    for (int py = y0; py < y1; ++py) {
        u32* row = pixels + static_cast<std::ptrdiff_t>(py) * rowWords;
        for (int px = x0; px < x1; ++px) row[px] = word;
    }
}

void Canvas::frame(float x, float y, float w, float h, float t, u32 rgb) {
    fill(x, y, w, t, rgb);
    fill(x, y + h - t, w, t, rgb);
    fill(x, y, t, h, rgb);
    fill(x + w - t, y, t, h, rgb);
}

float Canvas::textWidth(const char* s, float size) {
    float pen = 0.0f;
    for (; s != nullptr && *s != 0; ++s) {
        const font::Glyph* g = glyphFor(static_cast<unsigned char>(*s));
        if (g != nullptr) pen += static_cast<float>(g->advance16) / 16.0f;
    }
    return pen * size / static_cast<float>(font::kSize);
}

float Canvas::text(float x, float baseline, float size, const char* s, u32 rgb, u32 background) {
    const float f = size * scale / static_cast<float>(font::kSize);   // screen pixels per glyph pixel
    if (!(f > 0.0f)) return 0.0f;
    int samples = ceilInt(1.0f / f);
    if (samples < 1) samples = 1;
    if (samples > 4) samples = 4;
    const float step = 1.0f / static_cast<float>(samples), weight = 1.0f / static_cast<float>(samples * samples);
    float pen = x * scale;
    const float base = baseline * scale;
    for (; s != nullptr && *s != 0; ++s) {
        const font::Glyph* g = glyphFor(static_cast<unsigned char>(*s));
        if (g == nullptr) continue;
        if (g->width != 0 && g->height != 0) {
            const float gx = pen + static_cast<float>(g->left) * f;
            const float gy = base - static_cast<float>(font::kAscent - g->top) * f;
            int x0 = floorInt(gx), y0 = floorInt(gy);
            int x1 = ceilInt(gx + static_cast<float>(g->width) * f), y1 = ceilInt(gy + static_cast<float>(g->height) * f);
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > width) x1 = width;
            if (y1 > height) y1 = height;
            for (int py = y0; py < y1; ++py) {
                u32* row = pixels + static_cast<std::ptrdiff_t>(py) * rowWords;
                for (int px = x0; px < x1; ++px) {
                    float sum = 0.0f;
                    for (int j = 0; j < samples; ++j) {
                        const float sy = (static_cast<float>(py) + (static_cast<float>(j) + 0.5f) * step - gy) / f;
                        for (int i = 0; i < samples; ++i) {
                            const float sx = (static_cast<float>(px) + (static_cast<float>(i) + 0.5f) * step - gx) / f;
                            sum += coverage(*g, sx, sy);
                        }
                    }
                    const float a = sum * weight;
                    if (a > 0.004f) row[px] = pack(mix(background, rgb, a > 1.0f ? 1.0f : a));
                }
            }
        }
        pen += static_cast<float>(g->advance16) / 16.0f * f;
    }
    return pen / scale - x;
}

}  // namespace mm
