// SPDX-License-Identifier: GPL-3.0-or-later
#include "format.hpp"

#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "recomp/memory.hpp"

namespace recomp::hle {

std::string guest_format(const char* fmt, ArgReader& args) {
    std::string out;
    char spec[64];
    char buf[512];
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') {
            out += *p;
            continue;
        }
        const char* start = p++;
        if (*p == '%') {
            out += '%';
            continue;
        }
        // Flags, width, precision.
        std::string s = "%";
        while (*p && std::strchr("-+ #0'", *p)) s += *p++;
        if (*p == '*') {
            s += std::to_string(int32_t(args.u32()));
            ++p;
        } else {
            while (std::isdigit(uint8_t(*p))) s += *p++;
        }
        if (*p == '.') {
            s += *p++;
            if (*p == '*') {
                s += std::to_string(int32_t(args.u32()));
                ++p;
            } else {
                while (std::isdigit(uint8_t(*p))) s += *p++;
            }
        }
        // Length modifier.
        int len = 0;  // 0 = int, 1 = long (32-bit on guest), 2 = long long, -1 = short, -2 = char, 3 = long double
        if (*p == 'h') { len = -1; ++p; if (*p == 'h') { len = -2; ++p; } }
        else if (*p == 'l') { len = 1; ++p; if (*p == 'l') { len = 2; ++p; } }
        else if (*p == 'q' || *p == 'j') { len = 2; ++p; }
        else if (*p == 'z' || *p == 't') { len = 0; ++p; }
        else if (*p == 'L') { len = 3; ++p; }
        const char conv = *p;
        if (!conv) {
            out.append(start, p);
            break;
        }
        std::snprintf(spec, sizeof spec, "%s", s.c_str());
        const size_t sl = std::strlen(spec);
        auto emit = [&](auto value, const char* lenmod) {
            std::snprintf(spec + sl, sizeof spec - sl, "%s%c", lenmod, conv);
            const int n = std::snprintf(buf, sizeof buf, spec, value);
            if (n >= int(sizeof buf)) {
                std::string big(size_t(n) + 1, '\0');
                std::snprintf(big.data(), big.size(), spec, value);
                out.append(big.data(), size_t(n));
            } else if (n > 0) {
                out.append(buf, size_t(n));
            }
        };
        switch (conv) {
            case 'd': case 'i':
                if (len == 2) emit((long long)int64_t(args.u64()), "ll");
                else if (len == -1) emit(int(int16_t(args.u32())), "");
                else if (len == -2) emit(int(int8_t(args.u32())), "");
                else emit(int(int32_t(args.u32())), "");
                break;
            case 'u': case 'x': case 'X': case 'o':
                if (len == 2) emit((unsigned long long)args.u64(), "ll");
                else if (len == -1) emit(unsigned(uint16_t(args.u32())), "");
                else if (len == -2) emit(unsigned(uint8_t(args.u32())), "");
                else emit(unsigned(args.u32()), "");
                break;
            case 'c':
                emit(int(args.u32()), "");
                break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
                emit(args.f64(), "");  // long double == double on ARM
                break;
            case 's': {
                const uint32_t a = args.u32();
                if (len == 1) {
                    // Wide string: guest wchar_t is 32-bit, like the host's.
                    const wchar_t* w = a ? reinterpret_cast<const wchar_t*>(g_mem + a) : L"(null)";
                    std::snprintf(spec + sl, sizeof spec - sl, "ls");
                    const int n = std::snprintf(buf, sizeof buf, spec, w);
                    if (n > 0) out.append(buf, size_t(std::min<int>(n, int(sizeof buf) - 1)));
                } else {
                    const char* str = a ? mem::str(a) : "(null)";
                    std::snprintf(spec + sl, sizeof spec - sl, "s");
                    const int n = std::snprintf(nullptr, 0, spec, str);
                    if (n > 0) {
                        std::string tmp(size_t(n) + 1, '\0');
                        std::snprintf(tmp.data(), tmp.size(), spec, str);
                        out.append(tmp.data(), size_t(n));
                    }
                }
                break;
            }
            case 'p': {
                const uint32_t a = args.u32();
                std::snprintf(buf, sizeof buf, "0x%x", a);
                out += buf;
                break;
            }
            case 'n': {
                const uint32_t a = args.u32();
                if (a) st32(a, uint32_t(out.size()));
                break;
            }
            default:
                out.append(start, p + 1);
                break;
        }
    }
    return out;
}

int guest_scan(const char* input, const char* fmt, ArgReader& args) {
    const char* in = input;
    int assigned = 0;
    bool any_input = false;
    for (const char* f = fmt; *f; ++f) {
        if (std::isspace(uint8_t(*f))) {
            while (std::isspace(uint8_t(*in))) ++in;
            continue;
        }
        if (*f != '%') {
            if (*in != *f) break;
            ++in;
            continue;
        }
        ++f;
        if (*f == '%') {
            if (*in != '%') break;
            ++in;
            continue;
        }
        bool suppress = false;
        if (*f == '*') { suppress = true; ++f; }
        int width = 0;
        while (std::isdigit(uint8_t(*f))) width = width * 10 + (*f++ - '0');
        int len = 0;
        if (*f == 'h') { len = -1; ++f; if (*f == 'h') { len = -2; ++f; } }
        else if (*f == 'l') { len = 1; ++f; if (*f == 'l') { len = 2; ++f; } }
        else if (*f == 'L' || *f == 'q' || *f == 'j') { len = 2; ++f; }
        else if (*f == 'z' || *f == 't') { ++f; }
        const char conv = *f;
        if (conv != 'c' && conv != '[' && conv != 'n')
            while (std::isspace(uint8_t(*in))) ++in;
        if (!*in && conv != 'n') return any_input || assigned ? assigned : EOF;
        std::string field = width ? std::string(in, strnlen(in, size_t(width))) : std::string(in);
        char* end = nullptr;
        switch (conv) {
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                const int base = conv == 'd' || conv == 'u' ? 10 : conv == 'i' ? 0 : conv == 'o' ? 8 : 16;
                errno = 0;
                const long long v = conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o'
                                        ? (long long)std::strtoull(field.c_str(), &end, base)
                                        : std::strtoll(field.c_str(), &end, base);
                if (end == field.c_str()) return assigned;
                in += end - field.c_str();
                any_input = true;
                if (!suppress) {
                    const uint32_t a = args.u32();
                    if (len == 2) st64(a, uint64_t(v));
                    else if (len == -1) st16(a, uint32_t(v));
                    else if (len == -2) st8(a, uint32_t(v));
                    else st32(a, uint32_t(v));
                    ++assigned;
                }
                break;
            }
            case 'f': case 'e': case 'g': case 'E': case 'G': case 'a': {
                const double v = std::strtod(field.c_str(), &end);
                if (end == field.c_str()) return assigned;
                in += end - field.c_str();
                any_input = true;
                if (!suppress) {
                    const uint32_t a = args.u32();
                    if (len >= 1) st64(a, std::bit_cast<uint64_t>(v));
                    else st32(a, std::bit_cast<uint32_t>(float(v)));
                    ++assigned;
                }
                break;
            }
            case 's': {
                size_t n = 0;
                while (in[n] && !std::isspace(uint8_t(in[n])) && (!width || n < size_t(width))) ++n;
                if (!suppress) {
                    const uint32_t a = args.u32();
                    std::memcpy(g_mem + a, in, n);
                    g_mem[a + n] = 0;
                    ++assigned;
                }
                in += n;
                any_input = true;
                break;
            }
            case 'c': {
                const size_t n = width ? size_t(width) : 1;
                if (std::strlen(in) < n) return assigned;
                if (!suppress) {
                    std::memcpy(g_mem + args.u32(), in, n);
                    ++assigned;
                }
                in += n;
                any_input = true;
                break;
            }
            case '[': {
                ++f;
                bool negate = false;
                if (*f == '^') { negate = true; ++f; }
                std::string set;
                if (*f == ']') set += *f++;
                while (*f && *f != ']') set += *f++;
                auto in_set = [&](char ch) {
                    for (size_t i = 0; i < set.size(); ++i) {
                        if (i + 2 < set.size() && set[i + 1] == '-') {
                            if (ch >= set[i] && ch <= set[i + 2]) return true;
                            i += 2;
                        } else if (set[i] == ch) {
                            return true;
                        }
                    }
                    return false;
                };
                size_t n = 0;
                while (in[n] && in_set(in[n]) != negate && (!width || n < size_t(width))) ++n;
                if (n == 0) return assigned;
                if (!suppress) {
                    const uint32_t a = args.u32();
                    std::memcpy(g_mem + a, in, n);
                    g_mem[a + n] = 0;
                    ++assigned;
                }
                in += n;
                any_input = true;
                break;
            }
            case 'n':
                if (!suppress) st32(args.u32(), uint32_t(in - input));
                break;
            default:
                return assigned;
        }
    }
    return assigned;
}

}  // namespace recomp::hle
