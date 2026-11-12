// Exploration build only (-DMM_PROBE): find the world map's cursor. The world map part of the pause screen (class
// with its table of methods at main+0x4be1c88) gets logging wrappers on its methods: each logs its first calls and
// the object, and the object's memory is compared every 150 ms so the log shows what changes as the cursor moves.
#ifdef MM_PROBE
#include "common.hpp"
#include "menu.hpp"

extern "C" {
__attribute__((visibility("hidden"))) std::uintptr_t mm_vt_orig[16];
void mm_vt_note(int index, void* self);
}

#define MM_THUNK(K)                                                                                                  \
    asm(".text\n.balign 16\n.global mm_vt_thunk" #K "\n.hidden mm_vt_thunk" #K "\n.type mm_vt_thunk" #K              \
        ",%function\nmm_vt_thunk" #K ":\n"                                                                           \
        "stp x29, x30, [sp, #-0xe0]!\nmov x29, sp\nstp x0, x1, [sp, #0x10]\nstp x2, x3, [sp, #0x20]\n"            \
        "stp x4, x5, [sp, #0x30]\nstp x6, x7, [sp, #0x40]\nstr x8, [sp, #0x50]\nstp q0, q1, [sp, #0x60]\n"          \
        "stp q2, q3, [sp, #0x80]\nstp q4, q5, [sp, #0xa0]\nstp q6, q7, [sp, #0xc0]\n"                                \
        "mov x1, x0\nmov w0, #" #K "\nbl mm_vt_note\n"                                                               \
        "ldp q6, q7, [sp, #0xc0]\nldp q4, q5, [sp, #0xa0]\nldp q2, q3, [sp, #0x80]\nldp q0, q1, [sp, #0x60]\n"      \
        "ldr x8, [sp, #0x50]\nldp x6, x7, [sp, #0x40]\nldp x4, x5, [sp, #0x30]\nldp x2, x3, [sp, #0x20]\n"          \
        "ldp x0, x1, [sp, #0x10]\nldp x29, x30, [sp], #0xe0\n"                                                       \
        "adrp x16, mm_vt_orig\nadd x16, x16, :lo12:mm_vt_orig\nldr x16, [x16, #" #K "*8]\nbr x16\n");             \
    extern "C" void mm_vt_thunk##K();

MM_THUNK(2) MM_THUNK(3) MM_THUNK(4) MM_THUNK(5) MM_THUNK(7) MM_THUNK(9) MM_THUNK(10) MM_THUNK(11)

namespace mm {
namespace {

constexpr u64 kWorldMapVtable = 0x4be1c88;
template <typename T> T rd(std::uintptr_t a) { return *reinterpret_cast<const volatile T*>(a); }

u32 s_calls[16];
std::uintptr_t s_world = 0;
u32 s_snap[0x210 / 4];
bool s_snapValid = false;
u64 s_lastDiff = 0;
int s_lines = 0;

void diff(std::uintptr_t base) {
    char line[220];
    int n = 0, changes = 0;
    line[0] = 0;
    for (u32 i = 0; i < 0x210 / 4; ++i) {
        const u32 now = rd<u32>(base + 4u * i);
        if (s_snapValid && now != s_snap[i] && changes < 12) {
            float f; __builtin_memcpy(&f, &now, 4);
            const int whole = static_cast<int>(f * 100.0f);
            char item[44];
            formatText(item, sizeof(item), " +%x=%x(%d.%02d)", 4u * i, now, whole / 100, (whole < 0 ? -whole : whole) % 100);
            for (int k = 0; item[k] != 0 && n < static_cast<int>(sizeof(line)) - 1; ++k) line[n++] = item[k];
            line[n] = 0;
            ++changes;
        }
        s_snap[i] = now;
    }
    if (changes > 0 && s_lines < 500) { ++s_lines; log("probe world%s", line); }
    s_snapValid = true;
}

}  // namespace
}  // namespace mm

extern "C" void mm_vt_note(int index, void* self) {
    using namespace mm;
    const u32 c = ++s_calls[index & 15];
    if (isPowerOfTwo(c) && c <= 4096)
        log("probe world map method %d (main+%x): call %u, this %p", index, static_cast<unsigned>(mm_vt_orig[index] - g_main), c, self);
    const auto obj = reinterpret_cast<std::uintptr_t>(self);
    if (obj != s_world) {
        s_world = obj;
        s_snapValid = false;
        for (u32 i = 0; i < 0x210 / 4; i += 8) {
            log("probe world +%03x: %08x %08x %08x %08x %08x %08x %08x %08x", 4u * i, rd<u32>(obj + 4u * i), rd<u32>(obj + 4u * i + 4),
                rd<u32>(obj + 4u * i + 8), rd<u32>(obj + 4u * i + 12), rd<u32>(obj + 4u * i + 16), rd<u32>(obj + 4u * i + 20),
                rd<u32>(obj + 4u * i + 24), rd<u32>(obj + 4u * i + 28));
        }
    }
    const u64 now = ticksNow();
    if (now - s_lastDiff > tickRate() * 15 / 100) {
        s_lastDiff = now;
        diff(obj);
    }
}

namespace mm {

void installProbe() {
    auto* vt = reinterpret_cast<std::uintptr_t*>(g_main + kWorldMapVtable);
    const struct { int index; void (*thunk)(); } wraps[] = {
        {2, &mm_vt_thunk2}, {3, &mm_vt_thunk3}, {4, &mm_vt_thunk4}, {5, &mm_vt_thunk5},
        {7, &mm_vt_thunk7}, {9, &mm_vt_thunk9}, {10, &mm_vt_thunk10}, {11, &mm_vt_thunk11}};
    for (const auto& w : wraps) {
        mm_vt_orig[w.index] = vt[w.index];
        __atomic_store_n(&vt[w.index], reinterpret_cast<std::uintptr_t>(w.thunk), __ATOMIC_RELEASE);
    }
    log("probe: world map methods wrapped (method 2 was main+%x)", static_cast<unsigned>(mm_vt_orig[2] - g_main));
}

}  // namespace mm
#endif
