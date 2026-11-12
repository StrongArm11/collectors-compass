// Controller input for the settings menu.
//
// The game reads the controllers each frame with nn::hid::GetNpadStates (one call per connected controller, in the
// style it is used in), asking for the last 16 samples, newest first. The patch puts import hooks on those calls
// (hooks.json, "plt"); after the real call returns we look at the newest sample and may change the samples before
// the game sees them:
//  - On the pause map, L and R pressed together open the menu. So that the game doesn't react to the first of the
//    two, it is held back for a moment (kChordWindow); if the other one doesn't follow, the game gets it late, and a
//    quick tap that ended in that moment is replayed as a one-frame press.
//  - While the menu is open the game sees no buttons and centred sticks; the menu gets the presses instead.
//  - Buttons still held when the menu closes stay hidden from the game until they are let go (so the B that closes
//    the menu doesn't also close the pause screen).
#include "hints.hpp"
#include "hooks.hpp"
#include "menu.hpp"

namespace mm {
namespace {

struct NpadState {          // NpadFullKeyState and the other styles' states: 0x28 bytes (16 of them = the game's 0x280)
    s64 sampling;
    u64 buttons;
    s32 lx, ly, rx, ry;
    u32 attributes;
    u32 reserved;
};
static_assert(sizeof(NpadState) == 0x28);

using GetStatesFn = int (*)(NpadState* states, int count, const u32* id);
constexpr int kStyles = 5;
GetStatesFn s_original[kStyles] = {};
const char* const kStyleNames[kStyles] = {"Joy-Con pair", "Pro Controller", "left Joy-Con", "right Joy-Con", "handheld"};

constexpr int kIds = 10;    // controller numbers 0-7, "other" (0x10), handheld (0x20)
int slotFor(u32 id) { return id < 8 ? static_cast<int>(id) : id == 0x20 ? 8 : 9; }

struct Pad {
    u64 previous = 0;        // newest sample's buttons (with stick directions) last time
    u64 hidden = 0;          // held when the menu closed: hidden until let go
    u64 pending = 0;         // first half of the chord, held back
    u64 pendingSince = 0;
    u64 replay = 0;          // a held-back tap to hand to the game now
    s64 lastSampling = -1;
    bool seen = false;
};
Pad s_pads[kIds];
u32 s_styleSeen = 0;
bool s_orderLogged = false;

constexpr s32 kStickPress = 0x4000;     // about half way
constexpr u64 kStickLeft = 1ull << 40, kStickUp = 1ull << 41, kStickRight = 1ull << 42, kStickDown = 1ull << 43;
constexpr u64 kStickBits = kStickLeft | kStickUp | kStickRight | kStickDown;
constexpr u64 kRStickLeft = 1ull << 44, kRStickUp = 1ull << 45, kRStickRight = 1ull << 46, kRStickDown = 1ull << 47;
constexpr u64 kRStickBits = kRStickLeft | kRStickUp | kRStickRight | kRStickDown;

u64 withStick(const NpadState& s) {
    u64 b = s.buttons & 0xFFFFFFFFull;
    if (s.lx <= -kStickPress) b |= kStickLeft;
    if (s.lx >= kStickPress) b |= kStickRight;
    if (s.ly >= kStickPress) b |= kStickUp;
    if (s.ly <= -kStickPress) b |= kStickDown;
    if (s.rx <= -kStickPress) b |= kRStickLeft;
    if (s.rx >= kStickPress) b |= kRStickRight;
    if (s.ry >= kStickPress) b |= kRStickUp;
    if (s.ry <= -kStickPress) b |= kRStickDown;
    return b;
}

u64 chordWindow() { return tickRate() / 8; }   // 125 ms

void blankAll(NpadState* states, int count) {
    for (int i = 0; i < count; ++i) { states[i].buttons = 0; states[i].lx = states[i].ly = states[i].rx = states[i].ry = 0; }
}

void filter(int style, NpadState* states, int count, int returned, u32 id) {
    if (states == nullptr || count <= 0 || returned <= 0) return;
    if (returned > count) returned = count;
    Pad& pad = s_pads[slotFor(id)];
    if ((s_styleSeen & (1u << style)) == 0) {
        s_styleSeen |= 1u << style;
        log("controller %x read as %s (%d samples)", id, kStyleNames[style], returned);
    }
    if (!s_orderLogged && returned >= 2 && states[0].sampling != states[1].sampling) {
        s_orderLogged = true;
        log("controller samples %lld, %lld: %s first", static_cast<long long>(states[0].sampling),
            static_cast<long long>(states[1].sampling), states[0].sampling > states[1].sampling ? "newest" : "OLDEST");
    }
    const u64 now = ticksNow();
    const u64 raw = withStick(states[0]);
    const u64 pressed = pad.seen ? raw & ~pad.previous : 0;
    pad.previous = raw;
    pad.seen = true;
    const s64 lastSampling = pad.lastSampling;
#ifdef MM_PROBE
    {
        static s32 lastL[2], lastR[2];
        static u64 lastButtons;
        const NpadState& n = states[0];
        auto far = [](s32 a, s32 b) { return a - b > 0x2000 || b - a > 0x2000; };
        if (far(n.lx, lastL[0]) || far(n.ly, lastL[1]) || far(n.rx, lastR[0]) || far(n.ry, lastR[1]) || (n.buttons & 0xFFFF) != lastButtons) {
            lastL[0] = n.lx; lastL[1] = n.ly; lastR[0] = n.rx; lastR[1] = n.ry; lastButtons = n.buttons & 0xFFFF;
            log("probe input: buttons %x, left stick %d %d, right stick %d %d, map %s", static_cast<unsigned>(lastButtons), n.lx, n.ly, n.rx, n.ry, menuMapOpen() ? "open" : "closed");
        }
    }
#endif
    pad.lastSampling = states[0].sampling;

    if (menuIsOpen()) {
        if (!menuMapOpen()) {
            log("menu: the pause map closed; menu closed");
            menuClose();
        } else if ((raw & kMenuChord) == kMenuChord && (pressed & kMenuChord) != 0) {
            menuClose();
        } else {
            u64 menuPressed = pressed & 0xFFFF;
            if (pressed & kStickUp) menuPressed |= kBtnUp;
            if (pressed & kStickDown) menuPressed |= kBtnDown;
            if (pressed & kStickLeft) menuPressed |= kBtnLeft;
            if (pressed & kStickRight) menuPressed |= kBtnRight;
            menuInput(menuPressed);
        }
        if (!menuIsOpen()) pad.hidden = raw & 0xFFFFFFFFull;
        blankAll(states, count);
        return;
    }

    u64 replay = 0;
    if (menuMapOpen()) {
        if ((raw & kMenuChord) == kMenuChord && (pressed & kMenuChord) != 0) {
            pad.pending = 0;
            menuOpen();
            blankAll(states, count);
            return;
        }
        if (pad.pending == 0 && (pressed & kMenuChord) != 0) {
            pad.pending = pressed & kMenuChord;
            pad.pendingSince = now;
        } else if (pad.pending != 0) {
            if ((raw & pad.pending) == 0) {              // let go before the other button came: a tap
                replay = pad.pending;
                pad.pending = 0;
            } else if (now - pad.pendingSince > chordWindow()) {
                pad.pending = 0;                         // held: the game gets it now
            }
        }
        // hints: a tilt of the right stick picks the next icon (the game uses it only to zoom the world map; the
        // left stick changes floors on dungeon maps)
        const u64 tilt = pressed & kRStickBits;
        if (tilt != 0) {
            const int dx = (raw & kRStickRight) ? 1 : (raw & kRStickLeft) ? -1 : 0;
            const int dy = (raw & kRStickUp) ? 1 : (raw & kRStickDown) ? -1 : 0;
            if (dx != 0 || dy != 0) hintsNudge(dx, dy);
        }
    } else {
        pad.pending = 0;
    }
    pad.hidden &= raw;
    const u64 hide = pad.hidden | pad.pending;
    if (hide != 0) for (int i = 0; i < count; ++i) states[i].buttons &= ~hide;
    if (replay != 0) {
        bool any = false;
        for (int i = 0; i < returned; ++i) {
            if (lastSampling >= 0 && states[i].sampling <= lastSampling) break;
            states[i].buttons |= replay;
            any = true;
        }
        if (!any) states[0].buttons |= replay;
    }
}

template <int Style>
int onGetStates(NpadState* states, int count, const u32* id) {
    const int returned = s_original[Style](states, count, id);
    filter(Style, states, count, returned, id != nullptr ? *id : 0);
    return returned;
}

}  // namespace

// The controller is hooked through main's import table: each GetNpadStates entry is pointed at our handler, which
// calls what was there before. Whatever another mod puts in front of the import (Fast Travel patches the import
// stubs, Hyrule Cheat Menu the calls) then runs before us, and we run before the system. The entries are filled
// lazily the first time the game reads a controller, so inputTick (called every frame) takes each one over once it
// holds a real address.
namespace {
const GetStatesFn kHandlers[kStyles] = {&onGetStates<0>, &onGetStates<1>, &onGetStates<2>, &onGetStates<3>, &onGetStates<4>};
}

bool installInput() {
    log("controller: hooked through the import table once the game has read a controller");
    return true;
}

void inputTick() {
    if (g_main == 0) return;
    const u64 plt0 = g_main + kPlt0;
    for (int i = 0; i < kStyles; ++i) {
        u64* entry = reinterpret_cast<u64*>(g_main + kGotSlots[i].got);
        const u64 now = __atomic_load_n(entry, __ATOMIC_ACQUIRE);
        const u64 ours = reinterpret_cast<u64>(kHandlers[i]);
        if (now == ours || now == 0 || now == plt0) continue;
        __atomic_store_n(reinterpret_cast<u64*>(&s_original[i]), now, __ATOMIC_RELEASE);
        u64 expected = now;
        if (!__atomic_compare_exchange_n(entry, &expected, ours, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) continue;
        log("controller import %s hooked (calls on to %p)", kGotSlots[i].name, reinterpret_cast<void*>(now));
    }
}

}  // namespace mm
