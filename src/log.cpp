// Logging through svcOutputDebugString (Lichii writes it to user/log/eden_log.txt as "Debug.Emulated <Info>"),
// with a small printf of our own: %s %c %d %u %x %p, an l/ll size and a field width (e.g. %08x, %4d).
#include <cstdarg>

#include "common.hpp"

namespace mm {

std::uintptr_t g_main = 0;

namespace {

void outputDebugString(const char* text, std::size_t length) {
    register const char* x0 asm("x0") = text;
    register std::size_t x1 asm("x1") = length;
    asm volatile("svc #0x27" : "+r"(x0), "+r"(x1) : : "x2", "x3", "x4", "x5", "x6", "x7", "memory");
}

struct Out {
    char* p;
    char* end;
    void put(char c) { if (p < end) *p++ = c; }
};

void putNumber(Out& o, u64 v, unsigned base, int width, char pad, bool negative) {
    char tmp[24];
    int n = 0;
    do {
        const unsigned d = static_cast<unsigned>(v % base);
        tmp[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v != 0);
    if (negative) tmp[n++] = '-';
    for (int i = n; i < width; ++i) o.put(pad);
    while (n != 0) o.put(tmp[--n]);
}

void format(char* buf, std::size_t size, const char* f, va_list ap) {
    Out o{buf, buf + size - 1};
    for (; *f != 0; ++f) {
        if (*f != '%') { o.put(*f); continue; }
        ++f;
        char pad = ' ';
        if (*f == '0') { pad = '0'; ++f; }
        int width = 0;
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); ++f; }
        int longs = 0;
        while (*f == 'l') { ++longs; ++f; }
        switch (*f) {
        case 's': { const char* s = va_arg(ap, const char*); if (s == nullptr) s = "(null)"; while (*s != 0) o.put(*s++); break; }
        case 'c': o.put(static_cast<char>(va_arg(ap, int))); break;
        case 'd': {
            const long long v = longs != 0 ? va_arg(ap, long long) : va_arg(ap, int);
            putNumber(o, static_cast<u64>(v < 0 ? -v : v), 10, width, pad, v < 0);
            break;
        }
        case 'u': putNumber(o, longs != 0 ? va_arg(ap, u64) : va_arg(ap, unsigned), 10, width, pad, false); break;
        case 'x': putNumber(o, longs != 0 ? va_arg(ap, u64) : va_arg(ap, unsigned), 16, width, pad, false); break;
        case 'p': o.put('0'); o.put('x'); putNumber(o, reinterpret_cast<u64>(va_arg(ap, void*)), 16, 1, '0', false); break;
        case '%': o.put('%'); break;
        case 0: --f; break;
        default: o.put('?'); break;
        }
    }
    *o.p = 0;
}

}  // namespace

void formatText(char* buf, std::size_t size, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    format(buf, size, f, ap);
    va_end(ap);
}

void log(const char* f, ...) {
    char buf[320];
    static const char kPrefix[] = "[oot-minimap] ";
    std::size_t n = 0;
    for (; kPrefix[n] != 0; ++n) buf[n] = kPrefix[n];
    va_list ap;
    va_start(ap, f);
    format(buf + n, sizeof(buf) - n, f, ap);
    va_end(ap);
    std::size_t len = 0;
    while (buf[len] != 0) ++len;
    outputDebugString(buf, len);
}

}  // namespace mm

// The compiler may emit calls to these for struct copies and zeroing; the module has no C library of its own.
extern "C" {
__attribute__((used)) void* memset(void* d, int c, std::size_t n) {
    auto* p = static_cast<volatile unsigned char*>(d);
    while (n-- != 0) *p++ = static_cast<unsigned char>(c);
    return d;
}
__attribute__((used)) void* memcpy(void* d, const void* s, std::size_t n) {
    auto* p = static_cast<volatile unsigned char*>(d);
    const auto* q = static_cast<const unsigned char*>(s);
    while (n-- != 0) *p++ = *q++;
    return d;
}
__attribute__((used)) void* memmove(void* d, const void* s, std::size_t n) {
    auto* p = static_cast<volatile unsigned char*>(d);
    const auto* q = static_cast<const unsigned char*>(s);
    if (p < q) { while (n-- != 0) *p++ = *q++; }
    else { p += n; q += n; while (n-- != 0) *--p = *--q; }
    return d;
}
__attribute__((used)) int memcmp(const void* a, const void* b, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(a);
    const auto* q = static_cast<const unsigned char*>(b);
    for (; n != 0; --n, ++p, ++q) if (*p != *q) return *p - *q;
    return 0;
}
}
