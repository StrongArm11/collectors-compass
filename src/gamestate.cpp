#include "gamestate.hpp"

namespace mm {
namespace {

constexpr u64 kGameDataMgr = 0x4e842a8;   // main+: GameDataMgr*
constexpr u64 kScene = 0x4e919d0;         // main+: object with the current area's file path (char* at +0x40)
constexpr u64 kWorld = 0x4e7eab0;         // main+: world manager (+0x578 time manager)
constexpr u32 kChildhood = 0xcea220d3;    // flag "Sequence_IsChildhood"

template <typename T> T rd(std::uintptr_t a) { return *reinterpret_cast<const volatile T*>(a); }

// "…/KokiriForest_Adult.bcett.byml.zs" -> "KokiriForest"
void bancOf(const char* path, char* out, std::size_t size) {
    out[0] = 0;
    if (path == nullptr) return;
    const char* base = path;
    for (const char* p = path; *p != 0; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    std::size_t n = 0;
    while (base[n] != 0 && base[n] != '.' && base[n] != '_' && n + 1 < size) { out[n] = base[n]; ++n; }
    out[n] = 0;
}

}  // namespace

int gameBool(u32 hash) {
    const std::uintptr_t gdm = rd<std::uintptr_t>(g_main + kGameDataMgr);
    if (gdm == 0 || hash == 0) return -1;
    const u32 count = rd<u32>(gdm + 0x10), size = rd<u32>(gdm + 0x2c);
    const std::uintptr_t entries = rd<std::uintptr_t>(gdm + 0x18), index = rd<std::uintptr_t>(gdm + 0x20);
    if (count == 0 || size == 0 || entries == 0 || index == 0) return -1;
    u32 i = hash % size;
    for (u32 steps = 0; steps < size; ++steps) {
        const u32 h = rd<u32>(index + 8u * i), e = rd<u32>(index + 8u * i + 4);
        if (h == hash) return rd<u8>(entries + 24u * (e < count ? e : 0) + 0x16) & 1;
        if (h == 0) return -1;
        i = i + 1 == size ? 0 : i + 1;
    }
    return -1;
}

bool gameKey(u64 key) {
    const std::uintptr_t gdm = rd<std::uintptr_t>(g_main + kGameDataMgr);
    if (gdm == 0) return false;
    const u32 count = rd<u32>(gdm + 0xc58);
    const std::uintptr_t keys = rd<std::uintptr_t>(gdm + 0xc60);   // sorted 16-byte entries: key, ..., flags at +0xc
    if (count == 0 || keys == 0) return false;
    u32 lo = 0, hi = count;
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2;
        const u64 k = rd<u64>(keys + 16u * mid);
        if (k == key) return (rd<u8>(keys + 16u * mid + 0xc) & 1) != 0;
        if (k < key) lo = mid + 1; else hi = mid;
    }
    return false;
}

void readGameState(GameState& out) {
    const int c = gameBool(kChildhood);
    if (c >= 0) out.child = c == 1;
    out.hour = -1;
    const std::uintptr_t world = rd<std::uintptr_t>(g_main + kWorld);
    const std::uintptr_t timeMgr = world != 0 ? rd<std::uintptr_t>(world + 0x578) : 0;
    const std::uintptr_t clock = timeMgr != 0 ? rd<std::uintptr_t>(timeMgr + 0xd48) : 0;
    if (clock != 0) {
        const u8 flags = rd<u8>(clock + 9);
        const s32 t = rd<s32>(clock + ((flags & 2) != 0 ? 0x58 : 0x28));
        const s32 secondOfDay = ((t % 86400) + 86400) % 86400;
        out.hour = secondOfDay / 3600;
        out.minute = (secondOfDay / 60) % 60;
    }
    const std::uintptr_t scene = rd<std::uintptr_t>(g_main + kScene);
    bancOf(scene != 0 ? rd<const char*>(scene + 0x40) : nullptr, out.banc, sizeof(out.banc));
    out.valid = c >= 0;
}

}  // namespace mm
