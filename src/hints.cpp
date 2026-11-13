// The hint panel: a strip near the bottom of the pause map with the picked icon's tracker entry. The picking itself
// (which icon, its blinking) is done by the icon watcher (icons.cpp); this file keeps the stick tilts and paints.
#include "hints.hpp"

#include "menu.hpp"
#include "overlay.hpp"

namespace mm {
namespace {

std::atomic<u32> s_nudge{0};      // bit 31: new; low bits: dx + 1, (dy + 1) << 4
SpinLock s_lock;
HintInfo s_info;
u32 s_version = 1;
bool s_shown = false;

constexpr int kWidth = 1240, kHeightSteps = 352, kHeightShort = 196, kX = (1920 - kWidth) / 2, kBottomGap = 62, kTopY = 118;
constexpr int kMaxStepLines = 7;
constexpr u32 kBg = 0x15171c, kBorder = 0xb8964b, kTitle = 0xf2e7c9, kText = 0xe6e6e6, kSub = 0x969ca6,
              kMissing = 0x7fd892, kNotYet = 0xe8b04a, kCollected = 0x8b9099, kDivider = 0x3a3f4a;

bool same(const HintInfo& a, const HintInfo& b) {
    return a.id == b.id && a.status == b.status && a.why == b.why && a.position == b.position && a.total == b.total &&
           a.top == b.top && a.tokens == b.tokens && a.cat == b.cat;
}

// Word-wraps text into at most maxLines lines; returns how many were drawn.
int drawWrapped(Canvas& c, float x, float baseline, float lineHeight, float width, float size, const char* text, u32 rgb,
                u32 bg, int maxLines) {
    char line[160];
    int lines = 0;
    const char* p = text;
    while (*p != 0 && lines < maxLines) {
        int n = 0, lastSpace = -1;
        const char* q = p;
        while (*q != 0 && n < static_cast<int>(sizeof(line)) - 4) {
            line[n] = *q; line[n + 1] = 0;
            if (Canvas::textWidth(line, size) > width) break;
            if (*q == ' ') lastSpace = n;
            ++n; ++q;
        }
        if (*q != 0 && lastSpace > 0) { n = lastSpace; q = p + lastSpace + 1; }
        line[n] = 0;
        if (lines == maxLines - 1 && *q != 0) { line[n] = '.'; line[n + 1] = '.'; line[n + 2] = '.'; line[n + 3] = 0; }
        c.text(x, baseline + lineHeight * static_cast<float>(lines), size, line, rgb, bg);
        ++lines;
        p = q;
        while (*p == ' ') ++p;
    }
    return lines;
}

// Room for the steps: a line each, one more for each long one (they wrap), up to the most the panel shows.
int panelHeight(const HintInfo& info) {
    if (info.steps[0] == 0) return kHeightShort;
    int lines = 1, len = 0;
    for (const char* p = info.steps; *p != 0; ++p) {
        if (*p == '\n') { ++lines; len = 0; }
        else if (++len == 92) ++lines;
    }
    if (lines > kMaxStepLines) lines = kMaxStepLines;
    const int h = 92 + 33 * lines + 34;
    return h < kHeightSteps ? h : kHeightSteps;
}

const char* kindTitle(u8 cat) {
    switch (cat) {
    case 0: return "Gold Skulltula";
    case 1: return "Piece of Heart";
    case 2: return "Big Poe";
    default: return "Silver Rupees";
    }
}

void paintHint(Canvas& c) {
    HintInfo info;
    {
        LockGuard guard(s_lock);
        info = s_info;
    }
    const float w = static_cast<float>(kWidth), h = static_cast<float>(panelHeight(info)), pad = 30;
    c.fill(0, 0, w, h, kBg);
    c.frame(0, 0, w, h, 3, kBorder);
    char title[96];
    formatText(title, sizeof(title), "%s  %s", kindTitle(info.cat), info.id);
    float x = pad + c.text(pad, 44, 27, title, kTitle, kBg);
    if (info.area[0] != 0) c.text(x + 14, 44, 22, info.area, kSub, kBg);
    char status[96];
    u32 color = kMissing;
    switch (info.status) {
    case HintStatus::Missing:
        if (info.tokenTotal > 0) formatText(status, sizeof(status), "%d of %d collected", info.tokens, info.tokenTotal);
        else formatText(status, sizeof(status), "Missing");
        break;
    case HintStatus::NotYet: formatText(status, sizeof(status), "Not yet: %s", info.why); color = kNotYet; break;
    case HintStatus::Collected: formatText(status, sizeof(status), "Collected \x85"); color = kCollected; break;
    case HintStatus::Unknown: formatText(status, sizeof(status), "Can't tell if collected"); color = kSub; break;
    }
    c.text(w - pad - Canvas::textWidth(status, 23), 44, 23, status, color, kBg);
    c.fill(pad, 58, w - 2 * pad, 1, kDivider);
    float y = 92;
    if (info.steps[0] == 0) drawWrapped(c, pad, y, 30, w - 2 * pad, 23, info.desc, kText, kBg, 2);
    else {   // the steps (they include where it is): "1  ..." with wrapped lines indented, within the space left
        int linesLeft = kMaxStepLines, number = 1;
        const char* p = info.steps;
        char step[200];
        while (*p != 0 && linesLeft > 0) {
            int n = 0;
            while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(step)) - 1) { step[n] = p[n]; ++n; }
            step[n] = 0;
            p += n;
            while (*p == '\n') ++p;
            char num[8];
            formatText(num, sizeof(num), "%d", number++);
            c.text(pad + 4, y, 22, num, kBorder, kBg);
            const int used = drawWrapped(c, pad + 36, y, 29, w - 2 * pad - 36, 22, step, kText, kBg, linesLeft);
            y += 29.0f * static_cast<float>(used) + 4;
            linesLeft -= used;
        }
    }
    c.fill(pad, h - 46, w - 2 * pad, 1, kDivider);
    char foot[160];
    if (info.need[0] != 0) formatText(foot, sizeof(foot), "Needs: %s", info.need);
    else if (info.id[0] != 0) formatText(foot, sizeof(foot), "Needs nothing special");
    else foot[0] = 0;
    char nav[48];
    formatText(nav, sizeof(nav), "%d of %d   R stick: next", info.position, info.total);
    const float navWidth = Canvas::textWidth(nav, 20);
    const float room = w - 2 * pad - navWidth - 24;   // a long "Needs" line gets smaller type, then "..." at the end
    float size = 20;
    while (size > 15 && Canvas::textWidth(foot, size) > room) size -= 1;
    drawWrapped(c, pad, h - 16, 20, room, size, foot, kSub, kBg, 1);
    c.text(w - pad - navWidth, h - 16, 20, nav, kSub, kBg);
}

}  // namespace

void hintsNudge(int dx, int dy) {
    s_nudge.store(0x80000000u | static_cast<u32>(dx + 1) | static_cast<u32>(dy + 1) << 4, std::memory_order_release);
}

bool hintsTakeNudge(int& dx, int& dy) {
    const u32 v = s_nudge.exchange(0, std::memory_order_acq_rel);
    if ((v & 0x80000000u) == 0) return false;
    dx = static_cast<int>(v & 0xF) - 1;
    dy = static_cast<int>((v >> 4) & 0xF) - 1;
    return true;
}

void hintShow(const HintInfo& info) {
    if (menuIsOpen()) { s_shown = false; return; }   // the menu has the panel; shown again when it closes
    bool changed;
    {
        LockGuard guard(s_lock);
        changed = !s_shown || !same(info, s_info);
        s_info = info;
        if (changed) ++s_version;
    }
    if (changed) {
        s_shown = true;
        const int height = panelHeight(info);
        setPanel(true, kWidth, height, kX, info.top ? kTopY : 1080 - height - kBottomGap, s_version, &paintHint);
    }
}

void hintHide() {
    if (!s_shown) return;
    s_shown = false;
    if (!menuIsOpen()) setPanel(false);
}

}  // namespace mm
