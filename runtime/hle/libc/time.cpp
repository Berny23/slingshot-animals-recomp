// SPDX-License-Identifier: GPL-3.0-or-later
// <time.h> and <sys/time.h>. Guest time_t and long are 32-bit.
#include <cstring>
#include <ctime>

#include "host.hpp"
#include "libc.hpp"
#include "recomp/memory.hpp"

namespace recomp::hle {
namespace {

using u32 = uint32_t;

// UTC offset of a broken-down local time in seconds.
long gmt_offset(const std::tm& t) {
#ifdef _WIN32
    std::tm copy = t;
    const time_t local = _mkgmtime(&copy);  // the fields read as UTC
    copy = t;
    return long(local - std::mktime(&copy));
#else
    return t.tm_gmtoff;
#endif
}
bool gmtime(time_t t, std::tm& out) {
#ifdef _WIN32
    return ::gmtime_s(&out, &t) == 0;
#else
    return ::gmtime_r(&t, &out) != nullptr;
#endif
}
bool localtime(time_t t, std::tm& out) {
#ifdef _WIN32
    return ::localtime_s(&out, &t) == 0;
#else
    return ::localtime_r(&t, &out) != nullptr;
#endif
}

// bionic struct tm: 9 ints, long tm_gmtoff, const char* tm_zone (44 bytes)
void put_tm(u32 g, const std::tm& t) {
    const int f[9] = {t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    for (int i = 0; i < 9; ++i) st32(g + 4 * u32(i), u32(f[i]));
    st32(g + 36, u32(gmt_offset(t)));
    static u32 utc = mem::static_string("UTC");
    st32(g + 40, utc);
}
std::tm get_tm(u32 g) {
    std::tm t{};
    t.tm_sec = int32_t(ld32(g));
    t.tm_min = int32_t(ld32(g + 4));
    t.tm_hour = int32_t(ld32(g + 8));
    t.tm_mday = int32_t(ld32(g + 12));
    t.tm_mon = int32_t(ld32(g + 16));
    t.tm_year = int32_t(ld32(g + 20));
    t.tm_wday = int32_t(ld32(g + 24));
    t.tm_yday = int32_t(ld32(g + 28));
    t.tm_isdst = int32_t(ld32(g + 32));
    return t;
}

u32 h_time(u32 out) {
    const u32 t = u32(std::time(nullptr));
    if (out) st32(out, t);
    return t;
}
u32 h_clock() { return u32(std::clock()); }
int h_clock_gettime(int id, u32 ts) {
    const timespec t = host::clock(id);
    st32(ts, u32(t.tv_sec));
    st32(ts + 4, u32(t.tv_nsec));
    return 0;
}
int h_gettimeofday(u32 tv, u32 tz) {
    const timespec t = host::clock(0);
    if (tv) {
        st32(tv, u32(t.tv_sec));
        st32(tv + 4, u32(t.tv_nsec / 1000));
    }
    if (tz) {
        st32(tz, 0);
        st32(tz + 4, 0);
    }
    return 0;
}
u32 tm_buffer() {
    static thread_local u32 b = 0;
    if (!b) b = mem::alloc_zeroed(44);
    return b;
}
u32 h_gmtime_r(u32 tp, u32 out) {
    const time_t t = int32_t(ld32(tp));
    std::tm r{};
    gmtime(t, r);
    put_tm(out, r);
    return out;
}
u32 h_localtime_r(u32 tp, u32 out) {
    const time_t t = int32_t(ld32(tp));
    std::tm r{};
    localtime(t, r);
    put_tm(out, r);
    return out;
}
u32 h_gmtime(u32 tp) { return h_gmtime_r(tp, tm_buffer()); }
u32 h_localtime(u32 tp) { return h_localtime_r(tp, tm_buffer()); }
u32 h_mktime(u32 tmp) {
    std::tm t = get_tm(tmp);
    const time_t r = std::mktime(&t);
    put_tm(tmp, t);
    return u32(r);
}
double h_difftime(u32 a, u32 b) { return double(int32_t(a)) - double(int32_t(b)); }
u32 h_strftime(u32 buf, u32 n, u32 fmt, u32 tmp) {
    const std::tm t = get_tm(tmp);
    return u32(std::strftime(mem::str(buf), n, mem::str(fmt), &t));
}
u32 h_wcsftime(u32 buf, u32 n, u32 fmt, u32 tmp) {
    const std::tm t = get_tm(tmp);
    return u32(std::wcsftime(reinterpret_cast<wchar_t*>(g_mem + buf), n, reinterpret_cast<const wchar_t*>(g_mem + fmt), &t));
}

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
REG(time); REG(clock); REG(clock_gettime); REG(gettimeofday); REG(gmtime_r); REG(localtime_r); REG(gmtime);
REG(localtime); REG(mktime); REG(difftime); REG(strftime); REG(wcsftime);

}  // namespace
}  // namespace recomp::hle
