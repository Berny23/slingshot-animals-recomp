// SPDX-License-Identifier: GPL-3.0-or-later
// <string.h>, <ctype.h>, <wchar.h>, <wctype.h> and <stdlib.h> conversions.
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <strings.h>

#include "libc.hpp"
#include "recomp/hle.hpp"
#include "recomp/memory.hpp"

namespace recomp::hle {
namespace {

using mem::str;
using u32 = uint32_t;

inline void* P(u32 a) { return g_mem + a; }
inline u32 G(const void* p) { return p ? mem::guest(p) : 0; }
inline wchar_t* W(u32 a) { return reinterpret_cast<wchar_t*>(g_mem + a); }

// ---- memory
u32 h_memcpy(u32 d, u32 s, u32 n) { std::memmove(P(d), P(s), n); return d; }  // overlap-safe on purpose
u32 h_memmove(u32 d, u32 s, u32 n) { std::memmove(P(d), P(s), n); return d; }
u32 h_memset(u32 d, u32 c, u32 n) { std::memset(P(d), int(c), n); return d; }
// ARM EABI helpers: note the (dest, n, c) argument order of __aeabi_memset.
void h_aeabi_memcpy(u32 d, u32 s, u32 n) { std::memmove(P(d), P(s), n); }
void h_aeabi_memset(u32 d, u32 n, u32 c) { std::memset(P(d), int(c), n); }
void h_aeabi_memclr(u32 d, u32 n) { std::memset(P(d), 0, n); }
u32 h_memmem(u32 h, u32 hn, u32 n, u32 nn) {
    if (nn == 0) return h;
    void* r = ::memmem(P(h), hn, P(n), nn);
    return r ? mem::guest(r) : 0;
}
int h_memcmp(u32 a, u32 b, u32 n) { return std::memcmp(P(a), P(b), n); }
u32 h_memchr(u32 s, u32 c, u32 n) { return G(std::memchr(P(s), int(c), n)); }
u32 h_memrchr(u32 s, u32 c, u32 n) { return G(::memrchr(P(s), int(c), n)); }

// ---- strings
u32 h_strlen(u32 s) { return u32(std::strlen(str(s))); }
u32 h_strcpy(u32 d, u32 s) { std::strcpy(str(d), str(s)); return d; }
u32 h_strncpy(u32 d, u32 s, u32 n) { std::strncpy(str(d), str(s), n); return d; }
u32 h_strcat(u32 d, u32 s) { std::strcat(str(d), str(s)); return d; }
u32 h_strncat(u32 d, u32 s, u32 n) { std::strncat(str(d), str(s), n); return d; }
int h_strcmp(u32 a, u32 b) { return std::strcmp(str(a), str(b)); }
int h_strncmp(u32 a, u32 b, u32 n) { return std::strncmp(str(a), str(b), n); }
int h_strcasecmp(u32 a, u32 b) { return ::strcasecmp(str(a), str(b)); }
int h_strncasecmp(u32 a, u32 b, u32 n) { return ::strncasecmp(str(a), str(b), n); }
int h_strcoll(u32 a, u32 b) { return std::strcmp(str(a), str(b)); }
u32 h_strxfrm(u32 d, u32 s, u32 n) {
    const size_t l = std::strlen(str(s));
    if (n) std::strncpy(str(d), str(s), n);
    return u32(l);
}
u32 h_strchr(u32 s, u32 c) { return G(std::strchr(str(s), int(c))); }
u32 h_strrchr(u32 s, u32 c) { return G(std::strrchr(str(s), int(c))); }
u32 h_strstr(u32 a, u32 b) { return G(std::strstr(str(a), str(b))); }
u32 h_strpbrk(u32 a, u32 b) { return G(std::strpbrk(str(a), str(b))); }
u32 h_strspn(u32 a, u32 b) { return u32(std::strspn(str(a), str(b))); }
u32 h_strcspn(u32 a, u32 b) { return u32(std::strcspn(str(a), str(b))); }
u32 h_strdup(u32 s) { return mem::dup_string(str(s)); }
u32 h_strtok_r(u32 s, u32 delim, u32 save) {
    char* sp = ld32(save) ? str(ld32(save)) : nullptr;
    char* r = ::strtok_r(s ? str(s) : nullptr, str(delim), &sp);
    st32(save, G(sp));
    return G(r);
}
u32 h_strerror(u32 e) {
    // Per-call storage in guest memory (strings are small and rarely requested).
    static thread_local u32 buf = 0;
    if (!buf) buf = mem::alloc(256);
    std::snprintf(str(buf), 256, "%s", std::strerror(int(e)));
    return buf;
}
int h_strerror_r(u32 e, u32 buf, u32 n) {
    std::snprintf(str(buf), n, "%s", std::strerror(int(e)));
    return 0;
}

// ---- conversions
int h_atoi(u32 s) { return std::atoi(str(s)); }
int h_atol(u32 s) { return int(std::atol(str(s))); }
u32 h_strtol(u32 s, u32 endp, int base) {
    char* e;
    long long v = std::strtoll(str(s), &e, base);
    if (endp) st32(endp, G(e));
    // Guest long is 32-bit: saturate like the guest libc would.
    if (v > INT32_MAX) { set_guest_errno(ERANGE); v = INT32_MAX; }
    if (v < INT32_MIN) { set_guest_errno(ERANGE); v = INT32_MIN; }
    return u32(int32_t(v));
}
u32 h_strtoul(u32 s, u32 endp, int base) {
    char* e;
    unsigned long long v = std::strtoull(str(s), &e, base);
    if (endp) st32(endp, G(e));
    const char* p = str(s);
    while (std::isspace(uint8_t(*p))) ++p;
    if (*p != '-' && v > UINT32_MAX) { set_guest_errno(ERANGE); v = UINT32_MAX; }
    return u32(v);
}
int64_t h_strtoll(u32 s, u32 endp, int base) {
    char* e;
    const long long v = std::strtoll(str(s), &e, base);
    if (endp) st32(endp, G(e));
    return v;
}
uint64_t h_strtoull(u32 s, u32 endp, int base) {
    char* e;
    const unsigned long long v = std::strtoull(str(s), &e, base);
    if (endp) st32(endp, G(e));
    return v;
}
double h_strtod(u32 s, u32 endp) {
    char* e;
    const double v = std::strtod(str(s), &e);
    if (endp) st32(endp, G(e));
    return v;
}

// ---- ctype (functions; the macro tables are in data.cpp)
int h_isspace(int c) { return std::isspace(c & 0xff) != 0 && c != -1; }
int h_isupper(int c) { return c >= 'A' && c <= 'Z'; }
int h_isxdigit(int c) { return std::isxdigit(c & 0xff) != 0 && c != -1; }
int h_isdigit(int c) { return c >= '0' && c <= '9'; }
int h_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int h_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

// ---- wide characters (guest wchar_t is 32 bits, like the host's)
u32 h_wcslen(u32 s) { return u32(std::wcslen(W(s))); }
int h_wcscmp(u32 a, u32 b) { return std::wcscmp(W(a), W(b)); }
u32 h_wmemcpy(u32 d, u32 s, u32 n) { std::wmemcpy(W(d), W(s), n); return d; }
u32 h_wmemmove(u32 d, u32 s, u32 n) { std::wmemmove(W(d), W(s), n); return d; }
u32 h_wmemset(u32 d, u32 c, u32 n) { std::wmemset(W(d), wchar_t(c), n); return d; }
int h_wmemcmp(u32 a, u32 b, u32 n) { return std::wmemcmp(W(a), W(b), n); }
u32 h_wmemchr(u32 s, u32 c, u32 n) { return G(std::wmemchr(W(s), wchar_t(c), n)); }
int h_wcscoll(u32 a, u32 b) { return std::wcscmp(W(a), W(b)); }
u32 h_wcsxfrm(u32 d, u32 s, u32 n) {
    const size_t l = std::wcslen(W(s));
    if (n) std::wcsncpy(W(d), W(s), n);
    return u32(l);
}
u32 h_towlower(u32 c) { return u32(std::towlower(wint_t(c))); }
u32 h_towupper(u32 c) { return u32(std::towupper(wint_t(c))); }
u32 h_btowc(int c) { return c == -1 ? 0xffffffffu : u32(uint8_t(c)); }
int h_wctob(u32 c) { return c < 0x80 ? int(c) : -1; }
// Bionic (old) only supports the C locale: one byte per character.
u32 h_mbrtowc(u32 pwc, u32 s, u32 n, u32) {
    if (!s) return 0;
    if (n == 0) return u32(-2);
    const uint8_t ch = g_mem[s];
    if (pwc) st32(pwc, ch);
    return ch ? 1 : 0;
}
u32 h_wcrtomb(u32 s, u32 wc, u32) {
    if (!s) return 1;
    if (wc > 0xff) { set_guest_errno(EILSEQ); return u32(-1); }
    g_mem[s] = uint8_t(wc);
    return 1;
}
// wctype handles are small integers numbered here.
enum WType : u32 { WT_NONE, WT_ALNUM, WT_ALPHA, WT_BLANK, WT_CNTRL, WT_DIGIT, WT_GRAPH, WT_LOWER, WT_PRINT, WT_PUNCT,
                   WT_SPACE, WT_UPPER, WT_XDIGIT };
u32 h_wctype(u32 name) {
    static const char* names[] = {"", "alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print", "punct",
                                  "space", "upper", "xdigit"};
    for (u32 i = 1; i < sizeof names / sizeof *names; ++i)
        if (std::strcmp(str(name), names[i]) == 0) return i;
    return 0;
}
int h_iswctype(u32 c, u32 t) {
    const wint_t w = wint_t(c);
    switch (t) {
        case WT_ALNUM: return std::iswalnum(w) != 0;
        case WT_ALPHA: return std::iswalpha(w) != 0;
        case WT_BLANK: return std::iswblank(w) != 0;
        case WT_CNTRL: return std::iswcntrl(w) != 0;
        case WT_DIGIT: return std::iswdigit(w) != 0;
        case WT_GRAPH: return std::iswgraph(w) != 0;
        case WT_LOWER: return std::iswlower(w) != 0;
        case WT_PRINT: return std::iswprint(w) != 0;
        case WT_PUNCT: return std::iswpunct(w) != 0;
        case WT_SPACE: return std::iswspace(w) != 0;
        case WT_UPPER: return std::iswupper(w) != 0;
        case WT_XDIGIT: return std::iswxdigit(w) != 0;
        default: return 0;
    }
}

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
RECOMP_REGISTER_IMPORT("__aeabi_memcpy", RECOMP_HLE(h_aeabi_memcpy));
RECOMP_REGISTER_IMPORT("__aeabi_memcpy4", RECOMP_HLE(h_aeabi_memcpy));
RECOMP_REGISTER_IMPORT("__aeabi_memcpy8", RECOMP_HLE(h_aeabi_memcpy));
RECOMP_REGISTER_IMPORT("__aeabi_memmove", RECOMP_HLE(h_aeabi_memcpy));
RECOMP_REGISTER_IMPORT("__aeabi_memmove4", RECOMP_HLE(h_aeabi_memcpy));
RECOMP_REGISTER_IMPORT("__aeabi_memmove8", RECOMP_HLE(h_aeabi_memcpy));
RECOMP_REGISTER_IMPORT("__aeabi_memset", RECOMP_HLE(h_aeabi_memset));
RECOMP_REGISTER_IMPORT("__aeabi_memset4", RECOMP_HLE(h_aeabi_memset));
RECOMP_REGISTER_IMPORT("__aeabi_memset8", RECOMP_HLE(h_aeabi_memset));
RECOMP_REGISTER_IMPORT("__aeabi_memclr", RECOMP_HLE(h_aeabi_memclr));
RECOMP_REGISTER_IMPORT("__aeabi_memclr4", RECOMP_HLE(h_aeabi_memclr));
RECOMP_REGISTER_IMPORT("__aeabi_memclr8", RECOMP_HLE(h_aeabi_memclr));
REG(memmem);
REG(memcpy); REG(memmove); REG(memset); REG(memcmp); REG(memchr); REG(memrchr);
REG(strlen); REG(strcpy); REG(strncpy); REG(strcat); REG(strncat); REG(strcmp); REG(strncmp);
REG(strcasecmp); REG(strncasecmp); REG(strcoll); REG(strxfrm); REG(strchr); REG(strrchr); REG(strstr);
REG(strpbrk); REG(strspn); REG(strcspn); REG(strdup); REG(strtok_r); REG(strerror); REG(strerror_r);
REG(atoi); REG(atol); REG(strtol); REG(strtoul); REG(strtoll); REG(strtoull); REG(strtod);
REG(isspace); REG(isupper); REG(isxdigit); REG(isdigit); REG(tolower); REG(toupper);
REG(wcslen); REG(wcscmp); REG(wmemcpy); REG(wmemmove); REG(wmemset); REG(wmemcmp); REG(wmemchr); REG(wcscoll); REG(wcsxfrm);
REG(towlower); REG(towupper); REG(btowc); REG(wctob); REG(mbrtowc); REG(wcrtomb); REG(wctype); REG(iswctype);

}  // namespace
}  // namespace recomp::hle
