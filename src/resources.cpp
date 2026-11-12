// The mod's map icon pictures, patched into the game's UI archives in memory as the game loads them, so the mod
// ships none of the game's files.
//
// Every compressed resource the game reads goes through its decompressor (main+0xe8da0), which returns the whole
// decompressed file. Two of them are changed, by byte runs prepared from the game's own files
// (tools/gen_archive_patch.py -> build/archive_patch.inc):
//   - the UI layout archive (Common...blarc, a SARC): the Skulltula/heart map icon shows the plain pictures, not
//     the "checked" ones (tools/make_icons_romfs.py);
//   - the UI texture archive (Common...bntx): the facility picture the mod uses for Silver Rupee rooms is repainted
//     as a silver rupee (tools/make_icons_bntx.py).
// Each run keeps a hash of the original bytes; a run is applied only if every run of that file matches, so a
// different game version (or an already-patched file) is left alone.
#include "common.hpp"
#include "hooks.hpp"
#include "archive_patch.inc"

namespace mm {
namespace {

using DecompressFn = u8* (*)(void* self, void* loadArg, void* resource, u32* outSize, u32* outAllocSize, bool* outAllocated);
DecompressFn s_original = nullptr;

u64 fnv(const u8* p, u32 n) {
    u64 h = 0xcbf29ce484222325ULL;
    for (u32 i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ULL;
    return h;
}

template <std::size_t N>
void patchFile(const char* what, u8* buf, u32 size, const archive::Run (&runs)[N]) {
    int original = 0, already = 0;
    for (const auto& r : runs) {
        if (static_cast<u64>(r.offset) + r.length > size) break;
        const u64 h = fnv(buf + r.offset, r.length);
        if (h == r.oldHash) ++original;
        else if (h == r.newHash) ++already;
    }
    if (already == static_cast<int>(N)) { log("archive patch: %s already has the mod's pictures", what); return; }
    if (original != static_cast<int>(N)) {
        log("archive patch: %s doesn't match (%d of %d parts as expected); left as it is", what, original, static_cast<int>(N));
        return;
    }
    for (const auto& r : runs) __builtin_memcpy(buf + r.offset, archive::kData + r.data, r.length);
    log("archive patch: %s patched (%d parts)", what, static_cast<int>(N));
}

u8* onDecompress(void* self, void* loadArg, void* resource, u32* outSize, u32* outAllocSize, bool* outAllocated) {
    u8* buf = s_original(self, loadArg, resource, outSize, outAllocSize, outAllocated);
    if (buf == nullptr || outSize == nullptr) return buf;
    const u32 size = *outSize;
    if (size == archive::kLayoutSize && __builtin_memcmp(buf, archive::kLayoutMagic, 4) == 0)
        patchFile("UI layout archive", buf, size, archive::kLayoutRuns);
    else if (size == archive::kTexturesSize && __builtin_memcmp(buf, archive::kTexturesMagic, 4) == 0)
        patchFile("UI texture archive", buf, size, archive::kTexturesRuns);
    return buf;
}

}  // namespace

bool installResources() {
    return installHook(kHookDecompress, reinterpret_cast<void*>(&onDecompress), reinterpret_cast<void**>(&s_original));
}

}  // namespace mm

#ifdef MM_PROBE_HEAP
namespace nn { namespace mem {
class StandardAllocator {
public:
    __attribute__((weak)) std::size_t GetTotalFreeSize() const;
    __attribute__((weak)) std::size_t GetAllocatableSize() const;
};
}}
namespace mm {
// probe: how much room the game's malloc heap (an nn::mem::StandardAllocator at main+0x4d48660) has
void heapProbe(u64 frame) {
    static std::size_t minFree = ~0ull, minBlock = ~0ull;
    if (frame % 300 != 0) return;
    const auto* a = reinterpret_cast<const nn::mem::StandardAllocator*>(g_main + 0x4d48660);
    if (*reinterpret_cast<const u64*>(g_main + 0x4d48658) == 0) return;
    const std::size_t f = a->GetTotalFreeSize(), b = a->GetAllocatableSize();
    if (f < minFree) minFree = f;
    if (b < minBlock) minBlock = b;
    log("probe heap: malloc heap free %u KB, largest block %u KB (lowest so far %u / %u KB)", static_cast<u32>(f >> 10),
        static_cast<u32>(b >> 10), static_cast<u32>(minFree >> 10), static_cast<u32>(minBlock >> 10));
}
}  // namespace mm
#endif
