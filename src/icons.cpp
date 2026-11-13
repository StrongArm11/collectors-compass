// The game's own map icons for Gold Skulltulas and Pieces of Heart, on the pause map and the minimap.
//
// The pause map and the minimap share one icon manager class; its per-frame routine (main+0x209040) walks the
// Skulltula/heart entries of the current map and shows or hides an icon for each. The patch (hooks.json) puts an
// inline hook at the top of its per-entry loop: decide(manager, index) answers 0 = hide, 1 = the game's own
// (patched: "show if missing") rule, 2 = show.
//
// Each entry is matched to the tracker's data (Skulltulas by their save key, hearts by area and position), which
// says whether it's collected and what it needs. Missing ones are shown unless "only what I can get now" is on and
// Link can't get it yet (wrong age, needs night, or a missing tool); collected ones are shown only with "show
// collected". Decisions are worked out a few times a second in the routine's hook, before its loop runs.
//
// Looks: each icon (layout PaMapIconCollect_00) draws the plain icon (our layout points P_Icon_00 at it). After
// the routine runs we set its alpha: full for a missing item, faded for a collected one.
//
// Big Poes and Silver Rupee rooms have no icons of their own in the game, so the mod adds them to the map's
// facility icons (shops, mini-games: a list of up to 10 per map, drawn by the routine at main+0x202cf0 from the
// list's icon type): a Big Poe gets the game's Poe icon (type 30, the Poe Collector's), a Silver Rupee room type 3,
// whose picture (the Gerudo Training Ground sign) the mod's texture archive repaints as a silver rupee. The added
// entries go after the map's own, with an empty flag name (always shown) and a key starting 0x4D4D ("MM"), and are
// worked out again a few times a second like the others.
#include "icons.hpp"

#include "gamestate.hpp"
#include "hints.hpp"
#include "hooks.hpp"
#include "menu.hpp"
#include "settings.hpp"
#include "items.inc"

namespace mm {

Settings g_settings;
std::atomic<u32> g_settingsVersion{0};

namespace {

// ---- icon manager fields (build E0F3CCFA..., from the code that uses them) ----
constexpr std::uintptr_t kEntryCount = 0x368;    // s32: Skulltula/heart entries for the current map
constexpr std::uintptr_t kEntryArray = 0x1c8;    // entries, 0xc8 bytes each
constexpr std::uintptr_t kEntryStride = 0xc8;
constexpr std::uintptr_t kPartCount = 0xb8;      // u32: icon parts, one per entry
constexpr std::uintptr_t kPartArray = 0xc0;      // part pointers; part+0x20 = its layout (state byte at +0x3a)
constexpr std::uintptr_t kShowCollect = 0x45c;   // u8: 1 on the pause map, 0 on the minimap
// entry: +0 key (u64), +8 x, +0xc y, +0x10 z (floats), +0x14 type (6 heart, 7 Skulltula), +0x18 floor
constexpr s32 kTypeHeart = 6, kTypeSkulltula = 7;
constexpr int kMaxEntries = 64;
constexpr u64 kRecheckFrames = 15;               // about 4 times a second
constexpr float kCollectedAlpha = 0.45f;
// facility icons (shops, ...): the list, its icon parts, and the dungeon floor shown
constexpr std::uintptr_t kFacCap = 0x1e0;        // u32: capacity of the list (10)
constexpr std::uintptr_t kFacArray = 0x1e8;      // entries, 0xc0 bytes each
constexpr std::uintptr_t kFacCount = 0x370;      // s32: entries in use
constexpr std::uintptr_t kFacStride = 0xc0;
constexpr std::uintptr_t kFacParts = 0xe0;       // u32: icon parts (10), one per entry
constexpr std::uintptr_t kFacPartArray = 0xe8;
constexpr std::uintptr_t kFacHolder = 0x58;      // the pane the parts are shown under
constexpr std::uintptr_t kDungeonMode = 0x458;   // u8: 1 on a dungeon map (floors)
constexpr std::uintptr_t kFloorShown = 0x388;    // s32: the floor a dungeon map shows
// facility entry: +0 key, +8 x, +0xc y, +0x10 z, +0x14 floor, +0x18 icon type, +0x20 flag name (char*), +0x28 its
// buffer size, +0x2c the buffer (0x80 bytes); +0xb0.. other fields (copied from one of the game's entries)
constexpr s32 kIconPoe = 30, kIconRupee = 3;
constexpr s8 kNoFloor = -128;   // an item outside the dungeons
constexpr u64 kMineTag = 0x4D4D000000000000ULL, kMineMask = 0xFFFF000000000000ULL;
constexpr int kMaxMine = 10;
constexpr u64 kDetachPart = 0x2095e0;            // void (part, holder): takes a part off the map if it's shown there

// ---- game functions ----
constexpr u64 kFindPane = 0x43e1e0;              // pane* FindPane(layout, name)
constexpr u64 kSetAlpha = 0x2d8ac0;              // void Pane::SetAlpha(pane, float) - marks it for redraw
constexpr u64 kPlacedPane = 0x209650;            // pane* (layout + 0x40): the pane the routine moves to the icon's spot
constexpr std::uintptr_t kViewMode = 0x458;      // u32: byte +0x459 is 1 while the pause screen shows the world map
using FindPaneFn = void* (*)(void* layout, const char* name);
using SetAlphaFn = void (*)(void* pane, float alpha);

// Test builds: switch every shown icon between the two looks every few seconds, to check both on screen.
constexpr bool kPreviewLooks = false;
constexpr u64 kPreviewFrames = 240;

template <typename T> T rd(std::uintptr_t a) { return *reinterpret_cast<const volatile T*>(a); }

enum Decision : u8 { kHide = 0, kGameRule = 1, kShow = 2 };
enum Got : u8 { kMissing = 0, kCollected = 1, kUnknown = 2 };

struct Cache {
    std::uintptr_t mgr = 0, list = 0;
    s32 count = -1;
    // added facility entries
    std::uintptr_t facList = 0;
    s32 facVanilla = -1, facTotal = -1, facFloor = -2;
    int mine = 0;
    s16 mineItem[kMaxMine];
    u8 mineGot[kMaxMine];
    u8 mineTokens[kMaxMine];      // Silver Rupees: how many of the room's five are collected
    u64 frames = 0;
    u32 settingsSeen = 0;
    s16 item[kMaxEntries];
    u8 decision[kMaxEntries];
    u8 got[kMaxEntries];
    u8 logged[kMaxEntries];
};
constexpr int kMaxManagers = 4;
Cache s_cache[kMaxManagers];
GameState s_state;
char s_lastStateLog[96] = {};

struct PartPanes { std::uintptr_t part = 0; void* icon = nullptr; std::uintptr_t placed = 0; };
constexpr int kMaxParts = 160;
PartPanes s_panes[kMaxParts];
int s_paneCount = 0;
bool s_paneLogDone = false;

using CollectIconsFn = void (*)(void*);
CollectIconsFn s_original = nullptr;

int bancIndex(const char* banc) {
    for (int i = 0; i < data::kBancCount; ++i) if (sameText(banc, data::kBancs[i])) return i;
    return -1;
}

int matchItem(std::uintptr_t e, int banc) {
    const s32 type = rd<s32>(e + 0x14);
    if (type == kTypeSkulltula) {
        const u64 key = rd<u64>(e);
        for (int i = 0; i < data::kItemCount; ++i) if (data::kItems[i].cat == 0 && data::kItems[i].key == key) return i;
        return -1;
    }
    if (type != kTypeHeart) return -1;
    const float x = rd<float>(e + 8), z = rd<float>(e + 0x10);
    int best = -1; float bestD = 16.0f;   // within 4 m
    for (int pass = 0; pass < 2 && best < 0; ++pass) {   // same area first, then anywhere
        for (int i = 0; i < data::kItemCount; ++i) {
            const auto& it = data::kItems[i];
            if (it.cat != 1 || (pass == 0 && (banc < 0 || it.banc != banc))) continue;
            const float dx = it.x - x, dz = it.z - z, d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    return best;
}

int tokensGot(const data::Item& it) {
    int n = 0;
    for (int k = 0; k < it.tn; ++k) if (gameKey(data::kTokenKeys[it.t0 + k])) ++n;
    return n;
}

Got collected(const data::Item& it) {
    if (it.op == 3) return it.tn > 0 && tokensGot(it) == it.tn ? kCollected : kMissing;
    if (it.op == 1) { const int b = gameBool(static_cast<u32>(it.val)); return b < 0 ? kUnknown : b == 1 ? kCollected : kMissing; }
    if (it.op == 2) return gameKey(it.val) ? kCollected : kMissing;
    return kUnknown;
}

// Gold Skulltula tokens Link has: each tracked Skulltula counted once (some are listed for both the child and the
// adult map with the same save key).
int skulltulasGot() {
    int n = 0;
    for (int i = 0; i < data::kItemCount; ++i) {
        const auto& it = data::kItems[i];
        if (it.cat != 0) continue;
        bool seen = false;
        for (int j = 0; j < i && !seen; ++j) seen = data::kItems[j].cat == 0 && data::kItems[j].key == it.key;
        if (!seen && gameKey(it.val)) ++n;
    }
    return n;
}

// Rewards that only exist once Link has enough tokens: the House of Skulltula's Piece of Heart (50 tokens).
int tokensNeeded(const data::Item& it) { return sameText(it.id, "H-KAK4") ? 50 : 0; }

// Can Link get this now? Points why at the first thing missing.
bool canGetNow(const data::Item& it, const char** why) {
    *why = "";
    if (it.reqAge == 1 && !s_state.child) { *why = "needs child Link"; return false; }
    if (it.reqAge == 2 && s_state.child) { *why = "needs adult Link"; return false; }
    if (it.night != 0 && s_state.hour >= 0 && !s_state.night()) { *why = "needs night"; return false; }
    for (int g = 0; g < it.gn; ++g) {
        const auto& group = data::kReqGroups[it.g0 + g];
        bool have = false;
        for (int k = 0; k < group.count && !have; ++k) have = gameBool(data::kReqFlags[group.first + k]) == 1;
        if (!have) { *why = group.name; return false; }
    }
    return true;
}

Cache* cacheFor(std::uintptr_t mgr) {
    for (auto& c : s_cache) if (c.mgr == mgr) return &c;
    for (auto& c : s_cache) if (c.mgr == 0) { c.mgr = mgr; return &c; }
    return &s_cache[0];
}

void refresh(Cache& c, std::uintptr_t mgr, bool listChanged) {
    readGameState(s_state);
    char line[96];
    formatText(line, sizeof(line), "%s, %s (hour %d), area %s", s_state.child ? "child" : "adult",
               s_state.hour < 0 ? "time unknown" : s_state.night() ? "night" : "day", s_state.hour,
               s_state.banc[0] != 0 ? s_state.banc : "?");
    if (!sameText(line, s_lastStateLog)) {
        log("game state: %s", line);
        for (std::size_t i = 0; i < sizeof(line); ++i) s_lastStateLog[i] = line[i];
    }
    const int banc = bancIndex(s_state.banc);
    const std::uintptr_t list = rd<std::uintptr_t>(mgr + kEntryArray);
    const s32 count = rd<s32>(mgr + kEntryCount);
    for (s32 i = 0; i < count && i < kMaxEntries; ++i) {
        const std::uintptr_t e = list + static_cast<std::uintptr_t>(i) * kEntryStride;
        if (listChanged) { c.item[i] = static_cast<s16>(matchItem(e, banc)); c.logged[i] = 0xff; }
        const char* why = "";
        u8 d = kGameRule;
        Got got = kUnknown;
        const s32 type = rd<s32>(e + 0x14);
        const KindSettings* ks = type == kTypeSkulltula ? &g_settings.kind[kSkulltula] : type == kTypeHeart ? &g_settings.kind[kHeart] : nullptr;
        if (ks != nullptr && !ks->show) {
            d = kHide; why = "turned off in the menu";
        } else if (ks != nullptr && c.item[i] >= 0) {
            const auto& it = data::kItems[c.item[i]];
            got = collected(it);
            const int needTokens = tokensNeeded(it);
            if (got == kCollected) { d = ks->showCollected ? kShow : kHide; why = "collected"; }
            else if (needTokens > 0 && skulltulasGot() < needTokens) { d = kHide; why = "needs 50 Gold Skulltula tokens"; }
            else if (got == kMissing) d = (ks->onlyNow && !canGetNow(it, &why)) ? kHide : kShow;
            else if (ks->onlyNow && !canGetNow(it, &why)) d = kHide;   // unknown: the game's rule, unless out of reach
        }
        c.decision[i] = d;
        c.got[i] = got;
        const u8 note = static_cast<u8>(d | (got << 2));
        if (c.logged[i] != note) {
            c.logged[i] = note;
            log("  icon #%d (floor %d) %s %s at %d %d: %s, %s%s%s", i, rd<s32>(e + 0x18), type == kTypeSkulltula ? "Skulltula" : type == kTypeHeart ? "heart" : "?",
                c.item[i] >= 0 ? data::kItems[c.item[i]].id : "(not in tracker)", static_cast<int>(rd<float>(e + 8)),
                static_cast<int>(rd<float>(e + 0x10)), got == kCollected ? "collected" : got == kMissing ? "missing" : "status unknown",
                d == kHide ? "hidden" : d == kShow ? "shown" : "game's rule", why[0] != 0 ? ": " : "", why);
        }
    }
    c.list = list; c.count = count;
}

// ---- Big Poes and Silver Rupee rooms: added to the map's facility icons ----
u8 s_facTemplate[kFacStride];
bool s_haveFacTemplate = false;

std::uintptr_t facPart(std::uintptr_t mgr, s32 index) {
    const u32 parts = rd<u32>(mgr + kFacParts);
    const std::uintptr_t partArray = rd<std::uintptr_t>(mgr + kFacPartArray);
    if (partArray == 0 || index < 0 || static_cast<u32>(index) >= parts) return 0;
    return rd<std::uintptr_t>(partArray + 8u * static_cast<u32>(index));
}

// Puts our entries after the map's own (again, when the game has rebuilt the list or recompute is set).
void updateMine(Cache& c, std::uintptr_t mgr, bool recompute) {
    const std::uintptr_t list = rd<std::uintptr_t>(mgr + kFacArray);
    const s32 count = rd<s32>(mgr + kFacCount);
    const s32 cap = static_cast<s32>(rd<u32>(mgr + kFacCap)), parts = static_cast<s32>(rd<u32>(mgr + kFacParts));
    if (list == 0 || cap <= 0 || count < 0 || count > cap) return;
    s32 vanilla = count;   // where the game's own entries end
    for (s32 i = 0; i < count; ++i) {
        if ((rd<u64>(list + static_cast<std::uintptr_t>(i) * kFacStride) & kMineMask) == kMineTag) { vanilla = i; break; }
    }
    const bool dungeon = rd<u8>(mgr + kDungeonMode) == 1;
    const s32 floor = dungeon ? rd<s32>(mgr + kFloorShown) : -1;
    const bool fresh = list != c.facList || vanilla != c.facVanilla || count != c.facTotal || floor != c.facFloor;
    if (!fresh && !recompute) return;
    if (vanilla > 0) {
        __builtin_memcpy(s_facTemplate, reinterpret_cast<const void*>(list), kFacStride);
        s_haveFacTemplate = true;
    }
    // which of ours this map shows
    const int banc = bancIndex(s_state.banc);
    s32 room = (cap < parts ? cap : parts) - vanilla;
    if (room > kMaxMine) room = kMaxMine;
    int n = 0;
    s16 item[kMaxMine];
    u8 got[kMaxMine], tokens[kMaxMine];
    for (int i = 0; i < data::kItemCount && banc >= 0 && n < room; ++i) {
        const auto& it = data::kItems[i];
        if (it.cat < 2 || it.banc != banc) continue;
        if ((it.age == 1 && !s_state.child) || (it.age == 2 && s_state.child)) continue;
        if (it.floor != kNoFloor && dungeon && it.floor != floor) continue;
        const KindSettings& ks = g_settings.kind[it.cat == 2 ? kPoe : kRupee];
        if (!ks.show) continue;
        const Got g = collected(it);
        const char* why = "";
        if (g == kCollected ? !ks.showCollected : (ks.onlyNow && !canGetNow(it, &why))) continue;
        item[n] = static_cast<s16>(i);
        got[n] = g;
        tokens[n] = static_cast<u8>(it.op == 3 ? tokensGot(it) : 0);
        ++n;
    }
#ifdef MM_TEST_INJECT
    // test build: on any map without its own, show the first Big Poe and Silver Rupee room near the middle
    int testFirst = n;
    for (int i = 0; i < data::kItemCount && n < room && n < testFirst + 2; ++i) {
        const auto& it = data::kItems[i];
        if ((it.cat == 2 && n == testFirst) || (it.cat == 3 && n == testFirst + 1)) {
            item[n] = static_cast<s16>(i); got[n] = collected(it); tokens[n] = static_cast<u8>(it.op == 3 ? tokensGot(it) : 0); ++n;
        }
    }
#endif
    // write them
    for (int j = 0; j < n; ++j) {
        const auto& it = data::kItems[item[j]];
        const std::uintptr_t e = list + static_cast<std::uintptr_t>(vanilla + j) * kFacStride;
        u8* p = reinterpret_cast<u8*>(e);
        if (s_haveFacTemplate) __builtin_memcpy(p, s_facTemplate, kFacStride);
        else __builtin_memset(p, 0, kFacStride);
        *reinterpret_cast<u64*>(e) = kMineTag | static_cast<u64>(item[j]);
        *reinterpret_cast<float*>(e + 8) = it.x;
        *reinterpret_cast<float*>(e + 0xc) = it.y;
        *reinterpret_cast<float*>(e + 0x10) = it.z;
#ifdef MM_TEST_INJECT
        if (j >= testFirst) {   // near Link's house in Kokiri Forest (the test save's area)
            *reinterpret_cast<float*>(e + 8) = j == testFirst ? -40.0f : 40.0f;
            *reinterpret_cast<float*>(e + 0x10) = j == testFirst ? 0.0f : 10.0f;
        }
#endif
        *reinterpret_cast<s32*>(e + 0x14) = it.floor != kNoFloor ? it.floor : 0;
        *reinterpret_cast<s32*>(e + 0x18) = it.cat == 2 ? kIconPoe : kIconRupee;
        *reinterpret_cast<std::uintptr_t*>(e + 0x20) = e + 0x2c;   // no flag name: always shown
        *reinterpret_cast<s32*>(e + 0x28) = 0x80;
        __builtin_memset(p + 0x2c, 0, 0x80);
    }
    // parts of ours that are no longer used: off the map
    if (list == c.facList && c.facTotal > vanilla + n) {
        const std::uintptr_t holder = rd<std::uintptr_t>(mgr + kFacHolder);
        for (s32 k = vanilla + n; k < c.facTotal; ++k) {
            const std::uintptr_t part = facPart(mgr, k);
            if (part != 0 && holder != 0) reinterpret_cast<void (*)(std::uintptr_t, std::uintptr_t)>(g_main + kDetachPart)(part, holder);
        }
    }
    *reinterpret_cast<s32*>(mgr + kFacCount) = vanilla + n;
    if (fresh || n != c.mine) {
        log("facility icons %p: %d of the map's own, %d added (Big Poes / Silver Rupees); %s %d", reinterpret_cast<void*>(mgr), vanilla, n,
            dungeon ? "dungeon map, floor shown" : "floor", floor);
        for (int j = 0; j < n; ++j) {
            const auto& it = data::kItems[item[j]];
            log("  added #%d %s %s at %d %d: %s", vanilla + j, it.cat == 2 ? "Big Poe" : "Silver Rupees", it.id, static_cast<int>(it.x),
                static_cast<int>(it.z), got[j] == kCollected ? "collected" : "missing");
        }
    }
    c.facList = list; c.facVanilla = vanilla; c.facTotal = vanilla + n; c.facFloor = floor; c.mine = n;
    for (int j = 0; j < n; ++j) { c.mineItem[j] = item[j]; c.mineGot[j] = got[j]; c.mineTokens[j] = tokens[j]; }
}

PartPanes* panesFor(std::uintptr_t part, bool facility = false) {
    for (int i = 0; i < s_paneCount; ++i) if (s_panes[i].part == part) return &s_panes[i];
    if (s_paneCount >= kMaxParts) return nullptr;
    void* layout = rd<void*>(part + 0x20);
    if (layout == nullptr) return nullptr;
    const auto find = reinterpret_cast<FindPaneFn>(g_main + kFindPane);
    PartPanes& p = s_panes[s_paneCount++];
    p.part = part;
    p.icon = facility ? find(layout, "N_Icon_00") : nullptr;   // a facility icon: its picture and glow together
    if (p.icon == nullptr) p.icon = find(layout, "P_Icon_00");
    p.placed = reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(std::uintptr_t)>(g_main + kPlacedPane)(
        reinterpret_cast<std::uintptr_t>(layout) + 0x40));
    if (!s_paneLogDone) {
        s_paneLogDone = true;
        log("icon part %p: picture pane %p", reinterpret_cast<void*>(part), p.icon);
    }
    return &p;
}

// After the routine: give each shown icon its look.
void applyLooks(const Cache& c, std::uintptr_t mgr) {
    const u32 parts = rd<u32>(mgr + kPartCount);
    const std::uintptr_t partArray = rd<std::uintptr_t>(mgr + kPartArray);
    if (partArray == 0) return;
    const auto setAlpha = reinterpret_cast<SetAlphaFn>(g_main + kSetAlpha);
    const bool preview = kPreviewLooks && (c.frames / kPreviewFrames) % 2 == 1;
    for (s32 i = 0; i < c.count && i < kMaxEntries && static_cast<u32>(i) < parts; ++i) {
        const std::uintptr_t part = rd<std::uintptr_t>(partArray + 8u * static_cast<u32>(i));
        if (part == 0) continue;
        const std::uintptr_t layout = rd<std::uintptr_t>(part + 0x20);
        if (layout == 0 || rd<u8>(layout + 0x3a) == 0) continue;   // not shown
        PartPanes* p = panesFor(part);
        if (p == nullptr || p->icon == nullptr) continue;
        const bool collectedLook = c.got[i] == kCollected || preview;
        setAlpha(p->icon, collectedLook ? kCollectedAlpha : 1.0f);
#ifdef MM_PROBE_FRAMES
        {   // exploration: cycle the picture through the layout's four frames every 2.5 s
            const int frame = static_cast<int>((c.frames / 150) % 4);
            void* anim = rd<void*>(part + 0x48);
            reinterpret_cast<void (*)(void*, float)>(g_main + 0x2d2d90)(anim, static_cast<float>(frame));
            if (c.frames % 150 == 0 || c.frames % 150 == 1) {
                const float now = reinterpret_cast<float (*)(void*)>(g_main + 0x2d2b70)(rd<void*>(reinterpret_cast<std::uintptr_t>(anim) + 0x20));
                const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(anim);
                const std::uintptr_t inner = rd<std::uintptr_t>(a + 0x20);
                log("probe: picture frame %d -> animator frame %d/100; anim %p words %x %x %x %x %x %x %x %x | inner %x %x %x %x %x %x %x %x %x %x %x %x",
                    frame, static_cast<int>(now * 100), anim, rd<u32>(a), rd<u32>(a+4), rd<u32>(a+8), rd<u32>(a+12), rd<u32>(a+16), rd<u32>(a+20), rd<u32>(a+24), rd<u32>(a+28),
                    rd<u32>(inner), rd<u32>(inner+4), rd<u32>(inner+8), rd<u32>(inner+12), rd<u32>(inner+16), rd<u32>(inner+20), rd<u32>(inner+24), rd<u32>(inner+28), rd<u32>(inner+32), rd<u32>(inner+36), rd<u32>(inner+40), rd<u32>(inner+44));
            }
        }
#endif
    }
    for (int j = 0; j < c.mine; ++j) {   // ours among the facility icons
        const std::uintptr_t part = facPart(mgr, c.facVanilla + j);
        if (part == 0) continue;
        const std::uintptr_t layout = rd<std::uintptr_t>(part + 0x20);
        if (layout == 0 || rd<u8>(layout + 0x3a) == 0) continue;
        PartPanes* p = panesFor(part, true);
        if (p != nullptr && p->icon != nullptr) setAlpha(p->icon, c.mineGot[j] == kCollected ? kCollectedAlpha : 1.0f);
    }
}



// ---- hints: the right stick picks one of the shown icons on the pause map ----
// The pickable icons: the shown Skulltula/heart icons (index = entry) and our facility icons (index = 1000 + j).
constexpr int kMineBase = 1000;
struct Shown { int index; PartPanes* panes; float x, y; };
constexpr int kMaxShown = kMaxEntries + kMaxMine;

bool partShown(std::uintptr_t part) {
    if (part == 0) return false;
    const std::uintptr_t layout = rd<std::uintptr_t>(part + 0x20);
    return layout != 0 && rd<u8>(layout + 0x3a) != 0;
}

int listShown(const Cache& c, std::uintptr_t mgr, Shown* out) {
    int n = 0;
    const u32 parts = rd<u32>(mgr + kPartCount);
    const std::uintptr_t partArray = rd<std::uintptr_t>(mgr + kPartArray);
    for (int i = 0; partArray != 0 && i < c.count && i < kMaxEntries && static_cast<u32>(i) < parts; ++i) {
        const std::uintptr_t part = rd<std::uintptr_t>(partArray + 8u * static_cast<u32>(i));
        if (!partShown(part)) continue;
        PartPanes* p = panesFor(part);
        if (p == nullptr || p->icon == nullptr || p->placed == 0) continue;
        out[n++] = Shown{i, p, rd<float>(p->placed + 0x6c), rd<float>(p->placed + 0x70)};
    }
    for (int j = 0; j < c.mine && n < kMaxShown; ++j) {
        const std::uintptr_t part = facPart(mgr, c.facVanilla + j);
        if (!partShown(part)) continue;
        PartPanes* p = panesFor(part, true);
        if (p == nullptr || p->icon == nullptr || p->placed == 0) continue;
        out[n++] = Shown{kMineBase + j, p, rd<float>(p->placed + 0x6c), rd<float>(p->placed + 0x70)};
    }
    return n;
}

struct Pick { int index = -1; std::uintptr_t list = 0; u64 since = 0; };
Pick s_pick;
u32 s_lastViewMode = 0xFFFFFFFF;

// The best icon from (fromX, fromY) in direction (dx, dy): within 60 degrees of the tilt if there is one (else
// anywhere ahead), closest and nearest the line.
int pickToward(const Shown* shown, int count, int current, float fromX, float fromY, int dx, int dy) {
    const float norm = __builtin_sqrtf(static_cast<float>(dx * dx + dy * dy));
    for (int pass = 0; pass < 2; ++pass) {
        int best = -1;
        float bestScore = 1e30f;
        for (int k = 0; k < count; ++k) {
            if (k == current) continue;
            const float vx = shown[k].x - fromX, vy = shown[k].y - fromY;
            const float a = (vx * static_cast<float>(dx) + vy * static_cast<float>(dy)) / norm;   // distance ahead
            const float dist2 = vx * vx + vy * vy;
            if (a <= 1.0f) continue;                                   // not ahead
            if (pass == 0 && a * a < 0.25f * dist2) continue;          // more than 60 degrees off the tilt
            const float score = dist2 + 3.0f * (dist2 - a * a);
            if (score < bestScore) { bestScore = score; best = k; }
        }
        if (best >= 0) return best;
    }
    return -1;
}

void updateHints(const Cache& c, std::uintptr_t mgr, bool pauseMap) {
    const u32 view = rd<u32>(mgr + kViewMode);
    if (pauseMap && view != s_lastViewMode) {
        log("pause map view %x (%s)", view, (view & 0xFF00) != 0 ? "world map" : "area map");
        s_lastViewMode = view;
    }
    int dx = 0, dy = 0;
    const bool nudged = hintsTakeNudge(dx, dy);
    const bool worldMap = (view & 0xFF00) != 0;
    if (!pauseMap || worldMap || !g_settings.hints || menuIsOpen()) {
        if (!pauseMap || worldMap) s_pick.index = -1;
        hintHide();
        return;
    }
    if (s_pick.list != c.list) { s_pick.index = -1; s_pick.list = c.list; }
    Shown shown[kMaxShown];
    const int count = listShown(c, mgr, shown);
    int current = -1;
    for (int k = 0; k < count; ++k) if (shown[k].index == s_pick.index) current = k;
    if (current < 0) s_pick.index = -1;
    if (nudged && count > 0) {
        int next = -1;
        if (current < 0) {   // first tilt: the icon nearest the middle of the shown ones
            float cx = 0, cy = 0;
            for (int k = 0; k < count; ++k) { cx += shown[k].x; cy += shown[k].y; }
            cx /= static_cast<float>(count); cy /= static_cast<float>(count);
            float bestD = 1e30f;
            for (int k = 0; k < count; ++k) {
                const float d = (shown[k].x - cx) * (shown[k].x - cx) + (shown[k].y - cy) * (shown[k].y - cy);
                if (d < bestD) { bestD = d; next = k; }
            }
        } else {
            next = pickToward(shown, count, current, shown[current].x, shown[current].y, dx, dy);
        }
        if (next >= 0) {
            current = next;
            s_pick.index = shown[next].index;
            s_pick.since = c.frames;
            const int pi = s_pick.index >= kMineBase ? c.mineItem[s_pick.index - kMineBase] : c.item[s_pick.index];
            log("hint: picked icon #%d %s (map %d %d) after a tilt %d %d", s_pick.index, pi >= 0 ? data::kItems[pi].id : "(not in the list)",
                static_cast<int>(shown[next].x), static_cast<int>(shown[next].y), dx, dy);
        }
    }
    if (current < 0) { hintHide(); return; }
    const int index = s_pick.index;
    const bool mine = index >= kMineBase;
    const int item = mine ? c.mineItem[index - kMineBase] : c.item[index];
    const u8 got = mine ? c.mineGot[index - kMineBase] : c.got[index];
    // blink the picked icon
    const u64 t = (c.frames - s_pick.since) % 50;
    const float wave = t < 25 ? static_cast<float>(t) / 25.0f : static_cast<float>(50 - t) / 25.0f;   // 0..1..0
    const float base = got == kCollected ? kCollectedAlpha : 1.0f;
    reinterpret_cast<SetAlphaFn>(g_main + kSetAlpha)(shown[current].panes->icon, base * (1.0f - 0.7f * wave));
    // the panel
    HintInfo info;
    if (mine) {
        info.cat = data::kItems[item].cat;
    } else {
        const std::uintptr_t e = rd<std::uintptr_t>(mgr + kEntryArray) + static_cast<std::uintptr_t>(index) * kEntryStride;
        info.cat = rd<s32>(e + 0x14) == kTypeHeart ? 1 : 0;
    }
    info.position = current + 1;
    info.total = count;
    info.top = shown[current].y < -60.0f;   // low on the map: the panel goes to the top
    if (item < 0) {
        info.id = "";
        info.desc = "This one isn't in the mod's list.";
        info.status = HintStatus::Unknown;
    } else {
        const auto& it = data::kItems[item];
        info.id = it.id; info.area = it.area; info.desc = it.desc; info.need = it.need; info.steps = it.steps;
        if (it.op == 3) { info.tokens = mine ? c.mineTokens[index - kMineBase] : 0; info.tokenTotal = it.tn; }
        const char* why = "";
        if (got == kCollected) info.status = HintStatus::Collected;
        else if (!canGetNow(it, &why)) { info.status = HintStatus::NotYet; info.why = why; }
        else info.status = got == kUnknown ? HintStatus::Unknown : HintStatus::Missing;
    }
    hintShow(info);
}

#ifdef MM_PROBE
// ---- exploration build: what changes on the pause map while the player moves the sticks ----
struct Probe { std::uintptr_t base = 0; u32 size = 0; const char* name = ""; u32 words[0x180] = {}; bool valid = false; };
Probe s_probes[4];
int s_probeLines = 0;
void probeDiff(Probe& p, std::uintptr_t base, u32 size, const char* name) {
    if (base == 0) return;
    if (p.base != base) { p.base = base; p.size = size; p.name = name; p.valid = false; }
    char line[200];
    int n = 0, changes = 0;
    line[0] = 0;
    for (u32 i = 0; i < size / 4 && i < 0x180; ++i) {
        const u32 now = rd<u32>(base + 4u * i);
        if (p.valid && now != p.words[i] && changes < 10) {
            float f; __builtin_memcpy(&f, &now, 4);
            const int whole = static_cast<int>(f * 100.0f);
            char item[40];
            formatText(item, sizeof(item), " +%x=%x(%d.%02d)", 4u * i, now, whole / 100, (whole < 0 ? -whole : whole) % 100);
            for (int k = 0; item[k] != 0 && n < static_cast<int>(sizeof(line)) - 1; ++k) line[n++] = item[k];
            line[n] = 0;
            ++changes;
        }
        p.words[i] = now;
    }
    if (changes > 0 && s_probeLines < 400) { ++s_probeLines; log("probe %s%s", name, line); }
    p.valid = true;
}
void probeMap(std::uintptr_t mgr, u64 frame) {
    if (frame % 10 != 0) return;
    probeDiff(s_probes[0], mgr, 0x600, "mgr");
    const u32 parts = rd<u32>(mgr + kPartCount);
    const std::uintptr_t partArray = rd<std::uintptr_t>(mgr + kPartArray);
    if (parts == 0 || partArray == 0) return;
    const std::uintptr_t part = rd<std::uintptr_t>(partArray);
    if (part == 0) return;
    probeDiff(s_probes[1], rd<std::uintptr_t>(part + 0x20), 0x100, "layout0");
    PartPanes* p = panesFor(part);
    if (p != nullptr && p->icon != nullptr) probeDiff(s_probes[2], reinterpret_cast<std::uintptr_t>(p->icon), 0x200, "icon0");
    static void* root = nullptr;
    static std::uintptr_t rootFor = 0;
    if (rootFor != part) {
        rootFor = part;
        root = reinterpret_cast<FindPaneFn>(g_main + kFindPane)(rd<void*>(part + 0x20), "RootPane");
        log("probe: part0 %p layout %p icon pane %p root pane %p", reinterpret_cast<void*>(part), rd<void*>(part + 0x20), p != nullptr ? p->icon : nullptr, root);
    }
    if (root != nullptr) probeDiff(s_probes[3], reinterpret_cast<std::uintptr_t>(root), 0x200, "root0");
}
#endif

#ifdef MM_PROBE_HEAP
}  // namespace
void heapProbe(u64 frame);
namespace {
#endif
void onCollectIcons(void* self) {
    static bool settingsRead = false;
    if (!settingsRead) { settingsRead = true; settingsLoad(); }
    const auto mgr = reinterpret_cast<std::uintptr_t>(self);
    Cache& c = *cacheFor(mgr);
    const std::uintptr_t list = rd<std::uintptr_t>(mgr + kEntryArray);
    const s32 count = rd<s32>(mgr + kEntryCount);
    const bool listChanged = list != c.list || count != c.count;
    if (listChanged) {
        log("icons %p (%s): %d Skulltula/heart entries", self, rd<u8>(mgr + kShowCollect) != 0 ? "pause map" : "minimap", count);
#ifdef MM_PROBE_LISTS
        log("probe lists: collect cap %u count %d parts %u | facility cap %u count %d parts %u partcap %u | e0..f8 %x %x %x %x %x %x %x %x",
            rd<u32>(mgr + 0x1c0), count, rd<u32>(mgr + 0xb8), rd<u32>(mgr + 0x1e0), rd<s32>(mgr + 0x370), rd<u32>(mgr + 0xe0), rd<u32>(mgr + 0xec),
            rd<u32>(mgr + 0xe0), rd<u32>(mgr + 0xe4), rd<u32>(mgr + 0xe8), rd<u32>(mgr + 0xec), rd<u32>(mgr + 0xf0), rd<u32>(mgr + 0xf4), rd<u32>(mgr + 0xf8), rd<u32>(mgr + 0xfc));
        const std::uintptr_t fl = rd<std::uintptr_t>(mgr + 0x1e8);
        for (s32 k = 0; fl != 0 && k < rd<s32>(mgr + 0x370) && k < 6; ++k) {
            const std::uintptr_t f = fl + 0xc0u * static_cast<u32>(k);
            log("probe facility %d: id %llx xyz %d %d %d floor %d type %d name '%s' cap %d words20 %llx %x %x | b0 %llx b8 %llx",
                k, static_cast<unsigned long long>(rd<u64>(f)), static_cast<int>(rd<float>(f + 8)), static_cast<int>(rd<float>(f + 0xc)), static_cast<int>(rd<float>(f + 0x10)),
                rd<s32>(f + 0x14), rd<s32>(f + 0x18), rd<const char*>(f + 0x20), rd<s32>(f + 0x28),
                static_cast<unsigned long long>(rd<u64>(f + 0x20)), rd<u32>(f + 0x28), rd<u32>(f + 0x2c),
                static_cast<unsigned long long>(rd<u64>(f + 0xb0)), static_cast<unsigned long long>(rd<u64>(f + 0xb8)));
        }
        const std::uintptr_t fp = rd<std::uintptr_t>(mgr + 0xe8);
        for (u32 k = 0; fp != 0 && k < 3; ++k) log("probe facility part %u: %p", k, rd<void*>(fp + 8u * k));
#endif
    }
    const bool pauseMap = rd<u8>(mgr + kShowCollect) != 0;
    menuNoteMap(pauseMap);
    const u32 settings = g_settingsVersion.load(std::memory_order_acquire);
    const bool recompute = listChanged || settings != c.settingsSeen || c.frames % kRecheckFrames == 0;
    if (recompute) {
        c.settingsSeen = settings;
        refresh(c, mgr, listChanged);
    }
#ifdef MM_PROBE_HEAP
    heapProbe(c.frames);
#endif
    updateMine(c, mgr, recompute);
    s_original(self);
    applyLooks(c, mgr);
    updateHints(c, mgr, pauseMap);
#ifdef MM_PROBE
    if (pauseMap) probeMap(mgr, c.frames);
#endif
    if (kPreviewLooks && c.frames % kPreviewFrames == 0 && c.count > 0)
        log("preview: icons now show the %s look", (c.frames / kPreviewFrames) % 2 == 1 ? "collected (faded)" : "missing (plain)");
    ++c.frames;
}

// Called by the patch's trampoline for each entry, inside the routine above.
int decide(void* self, int index) {
    const auto mgr = reinterpret_cast<std::uintptr_t>(self);
    for (const auto& c : s_cache) {
        if (c.mgr == mgr) {
            if (index < 0 || index >= kMaxEntries || index >= c.count) return kGameRule;
            return c.decision[index];
        }
    }
    return kGameRule;
}

}  // namespace

bool installIcons() {
    const bool watcher = installHook(kHookCollectIcons, reinterpret_cast<void*>(&onCollectIcons), reinterpret_cast<void**>(&s_original));
    if (!watcher) return false;   // decisions are made in the watcher; without it the game's rule applies
    return installInline(kInlineCollectDecide, reinterpret_cast<void*>(&decide));
}

}  // namespace mm
