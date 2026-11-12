// Shared basics for Collector's Compass, the OoT collectibles map mod (a code module, subsdk9, loaded beside the game's main).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

namespace mm {

constexpr u64 kTitleId = 0x0400486024DEC000ULL;      // Ocarina of Time (Switch 2), game version 2.0.0
constexpr u64 kNnMainOffset = 0x1202CD0;             // nnMain inside main (build E0F3CCFA...)
constexpr u32 kMainMod0Offset = 0x3C4705C;           // main's MOD0 header offset, stored at main+4

// Lines in Lichii's log (user/log/eden_log.txt) start "[oot-minimap] ".
void log(const char* format, ...) __attribute__((format(printf, 1, 2)));
// Same formatting into a buffer (always zero-terminated).
void formatText(char* buf, std::size_t size, const char* format, ...) __attribute__((format(printf, 3, 4)));

// Start of the game's main module (set once at start-up).
extern std::uintptr_t g_main;

inline std::uintptr_t threadKey() {   // per-thread register every Horizon program has
    std::uintptr_t value;
    asm volatile("mrs %0, tpidrro_el0" : "=r"(value));
    return value;
}

constexpr bool sameText(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a != 0 && *a == *b) { ++a; ++b; }
    return *a == *b;
}
constexpr bool isPowerOfTwo(u64 v) { return v != 0 && (v & (v - 1)) == 0; }
constexpr std::size_t alignUp(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }

class SpinLock {
public:
    void lock() { while (m_flag.exchange(true, std::memory_order_acquire)) { asm volatile("yield"); } }
    void unlock() { m_flag.store(false, std::memory_order_release); }
private:
    std::atomic<bool> m_flag{false};
};
class LockGuard {
public:
    explicit LockGuard(SpinLock& l) : m_lock(l) { m_lock.lock(); }
    ~LockGuard() { m_lock.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    SpinLock& m_lock;
};

}  // namespace mm
