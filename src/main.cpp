// Collector's Compass (OoT collectibles on the maps). The game's loader (rtld) calls oot_minimap_init once at boot, before the game starts.
// The game's own Skulltula/heart icons on the minimap (patch in hooks.json, watcher in icons.cpp), and a settings
// menu on the pause map (menu.cpp: L + R; drawn by overlay.cpp, controller input in input.cpp).
// Lines in Lichii's log start "[oot-minimap]".
#include "common.hpp"
#include "icons.hpp"
#include "menu.hpp"
#include "overlay.hpp"

namespace {

u64 getInfo(u32 type, u32 handle, u64 subtype, u32* result) {
    register u64 x0 asm("x0");
    register u64 x1 asm("x1") = type;
    register u64 x2 asm("x2") = handle;
    register u64 x3 asm("x3") = subtype;
    asm volatile("svc #0x29" : "=r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "x4", "x5", "x6", "x7", "memory");
    *result = static_cast<u32>(x0);
    return x1;
}

}  // namespace

// Imports the game's loader links: nnMain from main (gives main's address). Weak: null if not linked.
extern "C" __attribute__((weak)) void nnMain();

#ifdef MM_PROBE
namespace mm { void installProbe(); }
#endif

namespace mm { bool installResources(); }

extern "C" __attribute__((used, visibility("default"))) void oot_minimap_init() {
    using namespace mm;
    u32 rc = 0;
    const u64 title = getInfo(18, 0xFFFF8001, 0, &rc);   // 18 = program id of the current process
    if (rc != 0 || title != kTitleId) {
        log("loaded, but this is not Ocarina of Time (title %llx, rc %x); doing nothing", static_cast<unsigned long long>(title), rc);
        return;
    }
    const auto entry = reinterpret_cast<std::uintptr_t>(&nnMain);
    if (entry == 0) { log("the loader did not link nnMain; doing nothing"); return; }
    const std::uintptr_t base = entry - kNnMainOffset;
    const u32 mod0 = *reinterpret_cast<const volatile u32*>(base + 4);
    if (mod0 != kMainMod0Offset || *reinterpret_cast<const volatile u32*>(base + mod0) != 0x30444F4D) {
        log("main at %p is not build E0F3CCFA (header %x); doing nothing", reinterpret_cast<void*>(base), mod0);
        return;
    }
    g_main = base;
    log("Collector's Compass 1.0.1 started (map icons, settings menu, hints), game code at %p", reinterpret_cast<void*>(base));
    if (installResources()) log("map icon pictures will be patched as the game loads its UI archives");
    if (installIcons()) log("icon watcher ready; missing Skulltulas and hearts should show on the minimap");
    const bool drawing = installOverlay();
    const bool input = installInput();
    if (drawing && input) log("settings menu ready: press L + R together on the pause map");
    else log("settings menu off (drawing %s, controller %s)", drawing ? "ok" : "missing", input ? "ok" : "missing");
#ifdef MM_PROBE
    installProbe();
#endif
}

#ifdef MM_PAD_KB
// test builds: extra static memory, to measure how much room is left for modules before the game won't start
__attribute__((used)) alignas(4096) unsigned char mm_pad[MM_PAD_KB * 1024];
#endif
