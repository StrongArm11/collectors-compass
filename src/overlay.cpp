// Drawing on top of the game's HUD.
//
// Adapted from Fast Travel's overlay (fast-travel/src/program/overlay.cpp in the oots2 mods repository, MIT License,
// copyright (c) 2026 HanSoloBlaster, afiaka87; see THIRD-PARTY-NOTICES.txt): the image is painted on the CPU into a
// linear texture (memory taken from the game's malloc heap, see below), in
// the screen image's format, and copied onto the screen image each time the "UI 2D" or "UI_2D_Overlay" layer binds it
// as a drawing target. Layers draw in order, so the copy lands on top of the HUD.
//
// The image is one panel (the settings menu, or a hint): whoever shows it gives its size in 1080p units, where it
// goes and a paint function; the panel is repainted on the drawing thread when its version changes, at the
// screen's scale, and copied opaque (the copy doesn't blend), so panels are solid rectangles.
#include "overlay.hpp"

#include "hooks.hpp"

namespace mm {
namespace {

// ---- graphics library (NVN) values, as Fast Travel uses them on this game ----
constexpr int kCopyEngine2D = 0x2;
constexpr int kCopyLinearFilter = 0x1;   // stretch smoothly when the sizes differ
constexpr int kBarrierOrderFragments = 0x2;
constexpr int kBarrierInvalidateTexture = 0x10;
constexpr int kPoolCpuUncached = 0x2;
constexpr int kPoolCpuCached = 0x4;
constexpr int kPoolGpuCached = 0x20;
constexpr int kTextureTarget2D = 0x1;
constexpr int kTextureFlagLinear = 0x10;
constexpr int kDeviceInfoLinearStrideAlignment = 0x38;
constexpr int kFormatRGBA8 = 0x25;
constexpr int kFormatRGBA8sRGB = 0x38;
constexpr int kFormatR11G11B10F = 0x3F;

struct CopyRegion { int x, y, z, width, height, depth; };
struct alignas(16) Opaque { unsigned char bytes[1024]; };   // graphics objects: generous storage

using GenericFn = void (*)();
using LookupFn = GenericFn (*)(const void* device, const char* name);
using Forward8Fn = void (*)(u64, u64, u64, u64, u64, u64, u64, u64);

struct Api {
    int (*textureWidth)(const void*);
    int (*textureHeight)(const void*);
    int (*textureFormat)(const void*);
    int (*textureTarget)(const void*);
    int (*textureSamples)(const void*);
    void (*copy)(void* cmd, const void* src, const void* srcView, const CopyRegion* srcRegion, const void* dst,
                 const void* dstView, const CopyRegion* dstRegion, int flags);
    void (*barrier)(void* cmd, int bits);
    void (*poolDefaults)(void*);
    void (*poolDevice)(void*, const void*);
    void (*poolStorage)(void*, void*, std::size_t);
    void (*poolFlags)(void*, int);
    u8 (*poolInit)(void*, const void*);
    void (*texDefaults)(void*);
    void (*texDevice)(void*, const void*);
    void (*texTarget)(void*, int);
    void (*texSize2D)(void*, int, int);
    void (*texFormat)(void*, int);
    void (*texFlags)(void*, int);
    void (*texStride)(void*, std::ptrdiff_t);
    std::size_t (*texStorageSize)(const void*);
    std::size_t (*texStorageAlignment)(const void*);
    void (*texStorage)(void*, const void*, std::ptrdiff_t);
    u8 (*texInit)(void*, const void*);
    void (*texFinalize)(void*);
    void* (*poolMap)(const void*);                              // optional
    void (*poolFlush)(const void*, std::ptrdiff_t, std::size_t);  // optional
    void (*deviceInteger)(const void*, int, int*);              // optional
};

LookupFn s_realLookup = nullptr;
const void* s_device = nullptr;
std::atomic<Forward8Fn> s_realSetRenderTargets{nullptr};
Api s_api{};
std::atomic<bool> s_apiReady{false};
bool s_apiFailed = false;

template <typename T>
bool resolve(T& out, const char* name, bool required = true) {
    out = reinterpret_cast<T>(s_realLookup(s_device, name));
    if (out == nullptr) log("graphics function %s not found%s", name, required ? "; nothing will be drawn" : "");
    return out != nullptr || !required;
}

void resolveApi() {
    if (s_apiReady.load() || s_apiFailed || s_realLookup == nullptr || s_device == nullptr) return;
    Api& a = s_api;
    const bool ok =
        resolve(a.textureWidth, "nvnTextureGetWidth") && resolve(a.textureHeight, "nvnTextureGetHeight") &&
        resolve(a.textureFormat, "nvnTextureGetFormat") && resolve(a.textureTarget, "nvnTextureGetTarget") &&
        resolve(a.textureSamples, "nvnTextureGetSamples") &&
        resolve(a.copy, "nvnCommandBufferCopyTextureToTexture") && resolve(a.barrier, "nvnCommandBufferBarrier") &&
        resolve(a.poolDefaults, "nvnMemoryPoolBuilderSetDefaults") &&
        resolve(a.poolDevice, "nvnMemoryPoolBuilderSetDevice") &&
        resolve(a.poolStorage, "nvnMemoryPoolBuilderSetStorage") &&
        resolve(a.poolFlags, "nvnMemoryPoolBuilderSetFlags") && resolve(a.poolInit, "nvnMemoryPoolInitialize") &&
        resolve(a.texDefaults, "nvnTextureBuilderSetDefaults") && resolve(a.texDevice, "nvnTextureBuilderSetDevice") &&
        resolve(a.texTarget, "nvnTextureBuilderSetTarget") && resolve(a.texSize2D, "nvnTextureBuilderSetSize2D") &&
        resolve(a.texFormat, "nvnTextureBuilderSetFormat") && resolve(a.texFlags, "nvnTextureBuilderSetFlags") &&
        resolve(a.texStride, "nvnTextureBuilderSetStride") &&
        resolve(a.texStorageSize, "nvnTextureBuilderGetStorageSize") &&
        resolve(a.texStorageAlignment, "nvnTextureBuilderGetStorageAlignment") &&
        resolve(a.texStorage, "nvnTextureBuilderSetStorage") && resolve(a.texInit, "nvnTextureInitialize") &&
        resolve(a.texFinalize, "nvnTextureFinalize") && resolve(a.poolMap, "nvnMemoryPoolMap", false) &&
        resolve(a.poolFlush, "nvnMemoryPoolFlushMappedRange", false) &&
        resolve(a.deviceInteger, "nvnDeviceGetInteger", false);
    if (!ok) { s_apiFailed = true; return; }
    s_apiReady.store(true);
    log("graphics functions resolved (device %p)", s_device);
}

// ---- which layer each drawing thread is in ----
enum class LayerKind : u8 { Other, Hud, HudOverlay };
struct LayerInfo { const void* layer = nullptr; LayerKind kind = LayerKind::Other; };
constexpr int kMaxLayers = 32;
LayerInfo s_layers[kMaxLayers];
std::atomic<int> s_layerCount{0};
SpinLock s_layerLock;
constexpr std::uintptr_t kLayerNameOffset = 0xE0;
constexpr std::uintptr_t kLayerIndexOffset = 0x94;

LayerKind kindFor(const char* name) {
    if (sameText(name, "UI 2D")) return LayerKind::Hud;
    if (sameText(name, "UI_2D_Overlay")) return LayerKind::HudOverlay;
    return LayerKind::Other;
}

LayerKind layerKind(const void* layer) {
    const int count = s_layerCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) if (s_layers[i].layer == layer) return s_layers[i].kind;
    LockGuard guard(s_layerLock);
    const int now = s_layerCount.load(std::memory_order_relaxed);
    for (int i = count; i < now; ++i) if (s_layers[i].layer == layer) return s_layers[i].kind;
    const char* name = *reinterpret_cast<const char* const*>(reinterpret_cast<std::uintptr_t>(layer) + kLayerNameOffset);
    const LayerKind kind = kindFor(name);
    if (now < kMaxLayers) {
        s_layers[now] = {layer, kind};
        s_layerCount.store(now + 1, std::memory_order_release);
        log("layer '%s' (index %u) first drawn%s", name != nullptr ? name : "(no name)",
            *reinterpret_cast<const u32*>(reinterpret_cast<std::uintptr_t>(layer) + kLayerIndexOffset),
            kind == LayerKind::Other ? "" : ": the overlay is copied onto its screen binds");
    }
    return kind;
}

struct ThreadSlot { std::atomic<std::uintptr_t> key{0}; LayerKind kind = LayerKind::Other; };
constexpr int kMaxSlots = 32;
ThreadSlot s_slots[kMaxSlots];
ThreadSlot* findSlot(std::uintptr_t key) {
    for (auto& slot : s_slots) if (slot.key.load(std::memory_order_acquire) == key) return &slot;
    return nullptr;
}
ThreadSlot* claimSlot(std::uintptr_t key) {
    for (auto& slot : s_slots) {
        std::uintptr_t expected = 0;
        if (slot.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) return &slot;
    }
    return nullptr;
}

// ---- the panel image: two linear textures (the copies read one while we paint the other) ----
// Their memory comes from the game's own malloc heap (36 MB, about 30 MB of it free in play), taken the first time a
// panel is shown and kept. Not from the mod's own static memory: the game sizes its main heap from the memory left
// after all the modules are loaded and stops at boot when that is too little, so every megabyte a mod reserves
// statically counts (Hyrule Cheat Menu alone reserves 36 MB). Each image is sized for the largest panel (the hint
// panel, 1240x352 at 1080p, or the menu, 760x572) on the screen at hand, at most 6 MB (8 MB in all); a panel that doesn't fit (a
// bigger screen later) is drawn smaller and stretched.
constexpr int kSlots = 2;
constexpr std::size_t kMaxSlotBytes = 6u << 20, kMinSlotBytes = 1u << 20, kMaxTotalBytes = 8u << 20, kFitMargin = 0x40000;
constexpr u64 kGameAlignedAlloc = 0x3eca0;        // main: aligned_alloc(alignment, size) on the game's malloc heap
unsigned char* s_imageMemory = nullptr;
std::size_t s_slotBytes = 0;
int s_slotCount = 0;
Opaque s_pool;
Opaque s_image[2];
int s_front = 0;
bool s_poolReady = false, s_poolCached = false, s_imageReady = false, s_imageFailed = false;
unsigned char* s_pixels = nullptr;
int s_imageFormat = -1, s_width = 0, s_height = 0, s_stride = 0;
std::size_t s_imageBytes = 0;
SpinLock s_imageLock;
u64 s_copies = 0;
u32 s_lut[3][256] = {};
u32 s_paintedVersion = 0;
const void* s_paintedFor = nullptr;

// What to show: set by the game's threads (setPanel), read by the drawing thread.
struct PanelRequest { bool visible = false; int width = 0, height = 0, x = -1, y = -1; u32 version = 0; PaintFn paint = nullptr; };
PanelRequest s_request;
SpinLock s_requestLock;

u32 floatBits(float v) { u32 b; __builtin_memcpy(&b, &v, 4); return b; }
u32 smallFloat(float v, int mantissaBits) {   // unsigned float with a 5-bit exponent
    if (!(v > 0.0f)) return 0;
    const u32 bits = floatBits(v);
    const int e = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
    if (e <= 0) return 0;
    if (e >= 31) return (30u << mantissaBits) | ((1u << mantissaBits) - 1);
    return (static_cast<u32>(e) << mantissaBits) | ((bits & 0x7FFFFF) >> (23 - mantissaBits));
}
float gammaDecode(float v) {   // about v^2.2
    float r = v * v, root = v;
    for (int i = 0; i < 3; ++i) root = __builtin_sqrtf(root);
    return r * root * root * (0.6f + 0.4f * root);
}
bool packable(int f) { return f == kFormatR11G11B10F || f == kFormatRGBA8 || f == kFormatRGBA8sRGB; }
void buildLut(int format) {
    if (format != kFormatR11G11B10F) return;
    for (int i = 0; i < 256; ++i) {
        const float lin = gammaDecode(static_cast<float>(i) / 255.0f);
        s_lut[0][i] = smallFloat(lin, 6);
        s_lut[1][i] = smallFloat(lin, 6) << 11;
        s_lut[2][i] = smallFloat(lin, 5) << 22;
    }
}

bool failImage(const char* what, long long value) {
    s_imageFailed = true;
    log("overlay image set-up failed at %s (%lld); nothing will be drawn", what, value);
    return false;
}

std::size_t panelBytes(int screenHeight) {
    const float k = static_cast<float>(screenHeight) / 1080.0f;
    auto bytes = [&](int w, int h) {
        return alignUp(static_cast<std::size_t>(w * k + 1) * 4, 512) * static_cast<std::size_t>(h * k + 1);
    };
    const std::size_t hint = bytes(1240, 352), menu = bytes(760, 572);
    return alignUp((hint > menu ? hint : menu) + kFitMargin, 0x10000);
}

// Takes the image memory (once) for a screen of this height.
SpinLock s_memoryLock;
std::atomic<bool> s_memoryReady{false};
bool ensureMemory(int screenHeight) {
    if (s_memoryReady.load(std::memory_order_acquire)) return true;
    LockGuard guard(s_memoryLock);   // the screen copies run on more than one thread: take the memory once
    if (s_imageMemory != nullptr) return true;
    if (s_imageFailed) return false;
    std::size_t slot = panelBytes(screenHeight);
    if (slot > kMaxSlotBytes) slot = kMaxSlotBytes;
    if (slot < kMinSlotBytes) slot = kMinSlotBytes;
    const auto alloc = reinterpret_cast<void* (*)(std::size_t, std::size_t)>(g_main + kGameAlignedAlloc);
    // at most 8 MB in all (a quarter of the heap's free room): on very large screens, one image
    for (int slots = slot * kSlots <= kMaxTotalBytes ? kSlots : 1; slots >= 1 && s_imageMemory == nullptr; --slots) {
        for (std::size_t size = slot; size >= kMinSlotBytes && s_imageMemory == nullptr; size /= 2) {
            void* p = alloc(4096, size * slots);
            if (p != nullptr) { s_imageMemory = static_cast<unsigned char*>(p); s_slotBytes = size; s_slotCount = slots; }
        }
    }
    if (s_imageMemory == nullptr) return failImage("memory from the game's malloc heap", static_cast<long long>(slot * kSlots));
    log("overlay memory: %d image(s) of %u KB from the game's malloc heap (screen height %d)", s_slotCount,
        static_cast<u32>(s_slotBytes >> 10), screenHeight);
    s_memoryReady.store(true, std::memory_order_release);
    return true;
}

bool ensurePool() {
    if (s_poolReady) return true;
    if (s_imageMemory == nullptr) return false;
    const Api& a = s_api;
    const int flagSets[2] = {kPoolCpuUncached | kPoolGpuCached, kPoolCpuCached | kPoolGpuCached};
    const std::size_t total = s_slotBytes * static_cast<std::size_t>(s_slotCount);
    for (const int flags : flagSets) {
        alignas(16) Opaque builder{};
        a.poolDefaults(&builder);
        a.poolDevice(&builder, s_device);
        a.poolStorage(&builder, s_imageMemory, total);
        a.poolFlags(&builder, flags);
        if (!a.poolInit(&s_pool, &builder)) { log("overlay memory pool with flags %x refused", flags); continue; }
        s_poolReady = true;
        s_poolCached = (flags & kPoolCpuCached) != 0;
        void* mapped = a.poolMap != nullptr ? a.poolMap(&s_pool) : nullptr;
        s_pixels = mapped != nullptr ? static_cast<unsigned char*>(mapped) : s_imageMemory;
        log("overlay memory pool ready: flags %x, CPU view %p (storage %p)", flags, s_pixels, s_imageMemory);
        return true;
    }
    return failImage("memory pool", static_cast<long long>(total));
}

bool ensureImage(int screenFormat, int width, int height) {
    const int format = packable(screenFormat) ? screenFormat : kFormatRGBA8;
    if (s_imageReady && format == s_imageFormat && width == s_width && height == s_height) return true;
    if (s_imageFailed || !ensurePool()) return false;
    const Api& a = s_api;
    if (s_imageReady) { for (int i = 0; i < s_slotCount; ++i) a.texFinalize(&s_image[i]); s_imageReady = false; }
    int alignment = 0;
    if (a.deviceInteger != nullptr) a.deviceInteger(s_device, kDeviceInfoLinearStrideAlignment, &alignment);
    const int used = (alignment >= 16 && alignment <= 4096 && isPowerOfTwo(static_cast<u64>(alignment))) ? alignment : 256;
    const int stride = static_cast<int>(alignUp(static_cast<std::size_t>(width) * 4, static_cast<std::size_t>(used)));
    alignas(16) Opaque builder{};
    a.texDefaults(&builder);
    a.texDevice(&builder, s_device);
    a.texTarget(&builder, kTextureTarget2D);
    a.texFormat(&builder, format);
    a.texSize2D(&builder, width, height);
    a.texFlags(&builder, kTextureFlagLinear);
    a.texStride(&builder, stride);
    const std::size_t bytes = a.texStorageSize(&builder), storageAlignment = a.texStorageAlignment(&builder);
    if (bytes == 0 || bytes > s_slotBytes || static_cast<std::size_t>(stride) * height > s_slotBytes ||
        s_slotBytes % (storageAlignment != 0 ? storageAlignment : 1) != 0)
        return failImage("image size", static_cast<long long>(bytes));
    for (int i = 0; i < s_slotCount; ++i) {
        a.texStorage(&builder, &s_pool, static_cast<std::ptrdiff_t>(i * s_slotBytes));
        if (!a.texInit(&s_image[i], &builder)) {
            for (int k = 0; k < i; ++k) a.texFinalize(&s_image[k]);
            return failImage("image", format);
        }
    }
    s_front = 0; s_imageReady = true; s_imageFormat = format; s_width = width; s_height = height; s_stride = stride;
    s_imageBytes = bytes; s_paintedFor = nullptr;
    buildLut(format);
    log("overlay images ready: %dx%d, format %d (screen format %d), row stride %d (device alignment %d), %llu bytes",
        width, height, format, screenFormat, stride, alignment, static_cast<unsigned long long>(bytes));
    return true;
}

// Only the screen: the largest widescreen target in the first one's format (see Fast Travel's notes: smaller
// targets such as the slingshot view, and square targets during area loads, must be skipped).
int s_screenWidth = 0, s_screenHeight = 0, s_screenFormat = -1, s_skipLogs = 0;
constexpr bool widescreen(int w, int h) {
    return static_cast<long long>(w) * 100 >= static_cast<long long>(h) * 170 &&
           static_cast<long long>(w) * 100 <= static_cast<long long>(h) * 185;
}
bool acceptScreen(int w, int h, int format) {
    if (w == s_screenWidth && h == s_screenHeight && format == s_screenFormat) return true;
    if (widescreen(w, h) && (s_screenFormat < 0 || format == s_screenFormat) &&
        static_cast<long long>(w) * h > static_cast<long long>(s_screenWidth) * s_screenHeight) {
        log("screen: %dx%d format %d%s", w, h, format, s_screenWidth > 0 ? " (larger than the one before)" : "");
        s_screenWidth = w; s_screenHeight = h; s_screenFormat = format;
        return true;
    }
    if (s_skipLogs < 8) {
        ++s_skipLogs;
        log("not drawing on a %dx%d format %d target (the screen is %dx%d format %d)%s", w, h, format, s_screenWidth,
            s_screenHeight, s_screenFormat, s_skipLogs == 8 ? "; not logged again" : "");
    }
    return false;
}

int roundToInt(float v) { return static_cast<int>(v + 0.5f); }

void copyOverlay(void* cmd, const void* screen, LayerKind kind) {
    PanelRequest req;
    {
        LockGuard guard(s_requestLock);
        req = s_request;
    }
    if (!req.visible || req.paint == nullptr || req.width <= 0 || req.height <= 0) return;
    const Api& a = s_api;
    const int width = a.textureWidth(screen), height = a.textureHeight(screen);
    if (a.textureSamples(screen) > 1 || a.textureTarget(screen) != kTextureTarget2D || width < 640 || height < 360) return;
    const int format = a.textureFormat(screen);
    const float screenScale = static_cast<float>(height) / 1080.0f;
    if (!ensureMemory(height)) return;
    float scale = screenScale;
    int w = roundToInt(req.width * scale), h = roundToInt(req.height * scale);
    int fullW = w, fullH = h;   // the size on screen; drawn smaller when it doesn't fit our memory, then stretched
    // Our image memory holds about a million pixels: on very large screens the panel is drawn a little smaller.
    const float fit = static_cast<float>(s_slotBytes - 0x40000) / (static_cast<float>(alignUp(static_cast<std::size_t>(w) * 4, 512)) * h);
    if (fit < 1.0f) {
        scale *= __builtin_sqrtf(fit);
        w = roundToInt(req.width * scale); h = roundToInt(req.height * scale);
    }
    if (w > width) w = width;
    if (h > height) h = height;
    if (fullW > width) fullW = width;
    if (fullH > height) fullH = height;
    int front = 0;
    {
        LockGuard guard(s_imageLock);
        if (!acceptScreen(width, height, format)) return;
        if (!ensureImage(format, w, h)) return;
        if (s_paintedVersion != req.version || s_paintedFor != reinterpret_cast<const void*>(req.paint)) {
            const int back = s_slotCount > 1 ? s_front ^ 1 : 0;
            Canvas canvas;
            canvas.pixels = reinterpret_cast<u32*>(s_pixels + back * s_slotBytes);
            canvas.rowWords = s_stride / 4;
            canvas.width = w;
            canvas.height = h;
            canvas.scale = scale;
            canvas.format = s_imageFormat;
            canvas.lut = s_lut;
            req.paint(canvas);
            if (s_poolCached && s_api.poolFlush != nullptr)
                s_api.poolFlush(&s_pool, static_cast<std::ptrdiff_t>(back * s_slotBytes), s_imageBytes);
            s_front = back;
            s_paintedVersion = req.version;
            s_paintedFor = reinterpret_cast<const void*>(req.paint);
        }
        front = s_front;
    }
    int x = req.x >= 0 ? roundToInt(req.x * screenScale) : (width - fullW) / 2;
    int y = req.y >= 0 ? roundToInt(req.y * screenScale) : (height - fullH) / 2;
    if (x + fullW > width) x = width - fullW;
    if (y + fullH > height) y = height - fullH;
    constexpr int kBarrier = kBarrierOrderFragments | kBarrierInvalidateTexture;
    const CopyRegion source{0, 0, 0, w, h, 1};
    const CopyRegion target{x, y, 0, fullW, fullH, 1};
    const int flags = kCopyEngine2D | (fullW != w || fullH != h ? kCopyLinearFilter : 0);
    a.barrier(cmd, kBarrier);
    a.copy(cmd, &s_image[front], nullptr, &source, screen, nullptr, &target, flags);
    a.barrier(cmd, kBarrier);
    const u64 copies = ++s_copies;
    if (isPowerOfTwo(copies) && copies <= (1u << 16))
        log("overlay copy %llu in %s: screen %p %dx%d format %d, panel %dx%d drawn %dx%d at (%d, %d)", static_cast<unsigned long long>(copies),
            kind == LayerKind::Hud ? "UI 2D" : "UI_2D_Overlay", screen, width, height, format, fullW, fullH, w, h, x, y);
}

// ---- hooks ----
void wrappedSetRenderTargets(u64 cmd, u64 count, u64 colors, u64 views, u64 depth, u64 depthView, u64 a6, u64 a7) {
    const auto* list = reinterpret_cast<const void* const*>(colors);
    if (s_apiReady.load(std::memory_order_acquire) && static_cast<int>(static_cast<u32>(count)) > 0 && list != nullptr &&
        list[0] != nullptr) {
        ThreadSlot* slot = findSlot(threadKey());
        if (slot != nullptr && slot->kind != LayerKind::Other) copyOverlay(reinterpret_cast<void*>(cmd), list[0], slot->kind);
    }
    s_realSetRenderTargets.load(std::memory_order_relaxed)(cmd, count, colors, views, depth, depthView, a6, a7);
}

GenericFn wrappedLookup(const void* device, const char* name) {
    const GenericFn real = s_realLookup(device, name);
    if (real == nullptr || name == nullptr) return real;
    if (sameText(name, "nvnCommandBufferSetRenderTargets")) {
        s_realSetRenderTargets.store(reinterpret_cast<Forward8Fn>(real));
        return reinterpret_cast<GenericFn>(&wrappedSetRenderTargets);
    }
    return real;
}

using LoadGraphicsProcsFn = void (*)(const void* device, LookupFn lookup);
using DrawLayerFn = void (*)(void* display, void* layer, u32 key);
LoadGraphicsProcsFn s_origLoadGraphicsProcs = nullptr;
DrawLayerFn s_origDrawLayer = nullptr;

void onLoadGraphicsProcs(const void* device, LookupFn lookup) {
    if (lookup == nullptr) {
        log("graphics table load without a lookup function; left alone");
        s_origLoadGraphicsProcs(device, lookup);
        return;
    }
    s_realLookup = lookup;
    if (device != nullptr) s_device = device;
    s_origLoadGraphicsProcs(device, &wrappedLookup);
    log("graphics table loaded (device %p): render-target bind %s", device, s_realSetRenderTargets.load() ? "wrapped" : "missing");
    resolveApi();
}

void onDrawLayer(void* display, void* layer, u32 key) {
    const LayerKind kind = layer != nullptr ? layerKind(layer) : LayerKind::Other;
    const std::uintptr_t thread = threadKey();
    ThreadSlot* slot = findSlot(thread);
    const bool nested = slot != nullptr;
    const LayerKind outer = nested ? slot->kind : LayerKind::Other;
    if (slot == nullptr) slot = claimSlot(thread);
    if (slot != nullptr) slot->kind = kind;
    s_origDrawLayer(display, layer, key);
    if (slot == nullptr) return;
    if (nested) {
        slot->kind = outer;
    } else {
        slot->kind = LayerKind::Other;
        slot->key.store(0, std::memory_order_release);
    }
}

}  // namespace

void setPanel(bool visible, int width, int height, int x, int y, u32 version, PaintFn paint) {
    LockGuard guard(s_requestLock);
    s_request.visible = visible;
    if (!visible) return;
    s_request.width = width; s_request.height = height; s_request.x = x; s_request.y = y;
    s_request.version = version; s_request.paint = paint;
}

bool installOverlay() {
    // The original is stored before the handler goes live (installHook writes it first).
    return installHook(kHookLoadGraphicsProcs, reinterpret_cast<void*>(&onLoadGraphicsProcs),
                       reinterpret_cast<void**>(&s_origLoadGraphicsProcs)) &&
           installHook(kHookDrawLayer, reinterpret_cast<void*>(&onDrawLayer), reinterpret_cast<void**>(&s_origDrawLayer));
}

}  // namespace mm
