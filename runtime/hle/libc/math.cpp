// SPDX-License-Identifier: GPL-3.0-or-later
// <math.h>. Doubles are passed in core register pairs (softfp); the HLE
// adapter takes care of that.
#include <cmath>

#include "recomp/hle.hpp"
#include "recomp/memory.hpp"

namespace recomp::hle {
namespace {

#define D1(name) double h_##name(double x) { return std::name(x); }
#define F1(name) float h_##name(float x) { return ::name(x); }
D1(acos) D1(asin) D1(atan) D1(ceil) D1(cos) D1(cosh) D1(exp) D1(floor) D1(log) D1(log10) D1(rint) D1(sin)
D1(sinh) D1(sqrt) D1(tan) D1(tanh)
F1(acosf) F1(asinf) F1(ceilf) F1(cosf) F1(floorf) F1(sinf) F1(sqrtf) F1(tanf)
double h_atan2(double y, double x) { return std::atan2(y, x); }
float h_atan2f(float y, float x) { return std::atan2(y, x); }
double h_fmod(double a, double b) { return std::fmod(a, b); }
float h_fmodf(float a, float b) { return std::fmod(a, b); }
double h_pow(double a, double b) { return std::pow(a, b); }
double h_ldexp(double x, int e) { return std::ldexp(x, e); }
double h_frexp(double x, uint32_t e) {
    int v;
    const double r = std::frexp(x, &v);
    st32(e, uint32_t(v));
    return r;
}
double h_modf(double x, uint32_t ip) {
    double i;
    const double r = std::modf(x, &i);
    st64(ip, std::bit_cast<uint64_t>(i));
    return r;
}
float h_modff(float x, uint32_t ip) {
    float i;
    const float r = std::modf(x, &i);
    st32(ip, std::bit_cast<uint32_t>(i));
    return r;
}
int32_t h_lrint(double x) { return int32_t(std::lrint(x)); }
int32_t h_lrintf(float x) { return int32_t(std::lrint(x)); }

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
REG(acos); REG(asin); REG(atan); REG(ceil); REG(cos); REG(cosh); REG(exp); REG(floor); REG(log); REG(log10);
REG(rint); REG(sin); REG(sinh); REG(sqrt); REG(tan); REG(tanh); REG(acosf); REG(asinf); REG(ceilf); REG(cosf);
REG(floorf); REG(sinf); REG(sqrtf); REG(tanf); REG(atan2); REG(atan2f); REG(fmod); REG(fmodf); REG(pow);
REG(ldexp); REG(frexp); REG(modf); REG(modff); REG(lrint); REG(lrintf);

}  // namespace
}  // namespace recomp::hle
