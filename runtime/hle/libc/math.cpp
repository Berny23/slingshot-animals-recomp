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
D1(sinh) D1(sqrt) D1(tan) D1(tanh) D1(acosh) D1(asinh) D1(atanh) D1(cbrt) D1(erf) D1(erfc) D1(exp2) D1(expm1)
D1(lgamma) D1(log1p) D1(logb) D1(nearbyint) D1(tgamma)
F1(acosf) F1(asinf) F1(atanf) F1(ceilf) F1(cosf) F1(floorf) F1(sinf) F1(sqrtf) F1(tanf) F1(coshf) F1(expf)
F1(log10f) F1(logf) F1(rintf) F1(sinhf) F1(tanhf)
double h_atan2(double y, double x) { return std::atan2(y, x); }
float h_atan2f(float y, float x) { return std::atan2(y, x); }
double h_fmod(double a, double b) { return std::fmod(a, b); }
float h_fmodf(float a, float b) { return std::fmod(a, b); }
double h_pow(double a, double b) { return std::pow(a, b); }
float h_powf(float a, float b) { return std::pow(a, b); }
double h_hypot(double a, double b) { return std::hypot(a, b); }
double h_nextafter(double a, double b) { return std::nextafter(a, b); }
float h_nextafterf(float a, float b) { return std::nextafter(a, b); }
double h_remainder(double a, double b) { return std::remainder(a, b); }
double h_remquo(double a, double b, uint32_t quo) {
    int q;
    const double r = std::remquo(a, b, &q);
    st32(quo, uint32_t(q));
    return r;
}
double h_scalbn(double x, int e) { return std::scalbn(x, e); }
float h_ldexpf(float x, int e) { return std::ldexp(x, e); }
int64_t h_llrint(double x) { return std::llrint(x); }
// bionic's classification values.
int h___fpclassifyd(double x) {
    switch (std::fpclassify(x)) {
        case FP_INFINITE: return 0x01;
        case FP_NAN: return 0x02;
        case FP_NORMAL: return 0x04;
        case FP_SUBNORMAL: return 0x08;
        default: return 0x10;  // FP_ZERO
    }
}
int h___isfinite(double x) { return std::isfinite(x); }
int h___signbit(double x) { return std::signbit(x); }
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
REG(rint); REG(sin); REG(sinh); REG(sqrt); REG(tan); REG(tanh); REG(acosf); REG(asinf); REG(atanf); REG(ceilf); REG(cosf);
REG(floorf); REG(sinf); REG(sqrtf); REG(tanf); REG(atan2); REG(atan2f); REG(fmod); REG(fmodf); REG(pow);
REG(ldexp); REG(frexp); REG(modf); REG(modff); REG(lrint); REG(lrintf); REG(acosh); REG(asinh); REG(atanh);
REG(cbrt); REG(erf); REG(erfc); REG(exp2); REG(expm1); REG(lgamma); REG(log1p); REG(logb); REG(nearbyint);
REG(tgamma); REG(coshf); REG(expf); REG(log10f); REG(logf); REG(rintf); REG(sinhf); REG(tanhf); REG(powf);
REG(hypot); REG(nextafter); REG(nextafterf); REG(remainder); REG(remquo); REG(scalbn); REG(ldexpf); REG(llrint);
REG(__fpclassifyd); REG(__isfinite); REG(__signbit);
RECOMP_REGISTER_IMPORT("scalbnl", RECOMP_HLE(h_scalbn));  // long double is double on ARM

}  // namespace
}  // namespace recomp::hle
