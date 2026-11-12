#include "hooks.hpp"

namespace mm {

bool installHook(HookId id, void* handler, void** original) {
    const HookSite& h = kHookSites[id];
    if (!h.enabled) { log("hook %s: not part of this build's patch; hook off", h.name); return false; }
    const auto* site = reinterpret_cast<const volatile u32*>(g_main + h.site);
    const u32 gateOffset = kGateArea + h.slot * 0x20;
    const u32 branch = 0x14000000u | (((gateOffset - h.site) >> 2) & 0x3FFFFFFu);
    if (site[0] != branch) {
        log("hook %s: main+%x holds %08x, not the patch's branch (is the .ips file installed?); hook off", h.name, h.site,
            site[0]);
        return false;
    }
    for (int i = 1; i < 4; ++i) {
        if (site[i] != h.words[i]) {
            log("hook %s: main+%x word %d is %08x, expected %08x (different game version?); hook off", h.name, h.site, i,
                site[i], h.words[i]);
            return false;
        }
    }
    const auto* gate = reinterpret_cast<const volatile u32*>(g_main + gateOffset);
    if (gate[4] != h.words[0]) {
        log("hook %s: its gate holds %08x, expected the routine's first word %08x; hook off", h.name, gate[4], h.words[0]);
        return false;
    }
    *original = const_cast<u32*>(gate + 4);
    __atomic_store_n(reinterpret_cast<u64*>(g_main + kHookTable + h.slot * 8), reinterpret_cast<u64>(handler),
                     __ATOMIC_RELEASE);
    log("hook %s at main+%x installed", h.name, h.site);
    return true;
}

}  // namespace mm

namespace mm {

bool installInline(InlineId id, void* handler) {
    const InlineSite& h = kInlineSites[id];
    const auto* site = reinterpret_cast<const volatile u32*>(g_main + h.site);
    const u32 branch = 0x14000000u | (((h.trampoline - h.site) >> 2) & 0x3FFFFFFu);
    if (site[0] != branch) {
        log("inline hook %s: main+%x holds %08x, not the patch's branch (is the .ips file installed?); hook off", h.name,
            h.site, site[0]);
        return false;
    }
    const auto* tramp = reinterpret_cast<const volatile u32*>(g_main + h.trampoline);
    if (tramp[0] != 0xA9BF7BFDu) {
        log("inline hook %s: its trampoline holds %08x; hook off", h.name, tramp[0]);
        return false;
    }
    __atomic_store_n(reinterpret_cast<u64*>(g_main + kHookTable + h.slot * 8), reinterpret_cast<u64>(handler),
                     __ATOMIC_RELEASE);
    log("inline hook %s at main+%x installed", h.name, h.site);
    return true;
}

}  // namespace mm

namespace mm {

bool installPlt(PltId id, void* handler, void** original) {
    const PltSite& h = kPltSites[id];
    const auto* site = reinterpret_cast<const volatile u32*>(g_main + h.site);
    const u32 branch = 0x14000000u | (((h.gate - h.site) >> 2) & 0x3FFFFFFu);
    if (site[0] != branch) {
        log("import hook %s: main+%x holds %08x, not the patch's branch (is the .ips file installed?); hook off", h.name,
            h.site, site[0]);
        return false;
    }
    for (int i = 1; i < 4; ++i) {
        if (site[i] != h.words[i]) {
            log("import hook %s: main+%x word %d is %08x, expected %08x; hook off", h.name, h.site, i, site[i], h.words[i]);
            return false;
        }
    }
    const auto* gate = reinterpret_cast<const volatile u32*>(g_main + h.gate);
    if (gate[5] != h.words[1] || gate[6] != h.words[2] || gate[7] != h.words[3]) {
        log("import hook %s: its gate does not hold the stub copy; hook off", h.name);
        return false;
    }
    *original = const_cast<u32*>(gate + 4);
    __atomic_store_n(reinterpret_cast<u64*>(g_main + kHookTable + h.slot * 8), reinterpret_cast<u64>(handler),
                     __ATOMIC_RELEASE);
    log("import hook %s at main+%x installed", h.name, h.site);
    return true;
}

}  // namespace mm
