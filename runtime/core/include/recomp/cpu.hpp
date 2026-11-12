// SPDX-License-Identifier: GPL-3.0-or-later
// Guest CPU state and the primitive operations used by generated code.
//
// Generated functions have the signature `void fn(recomp::CPU* ctx)`. All guest
// state lives in CPU; guest memory is a flat 4 GiB region at recomp::g_mem, so
// guest address `a` is host address `g_mem + a`.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(__clang__)
#define RECOMP_MUSTTAIL [[clang::musttail]]
#elif defined(__GNUC__) && __GNUC__ >= 15
#define RECOMP_MUSTTAIL [[gnu::musttail]]
#else
#define RECOMP_MUSTTAIL
#endif

#define RECOMP_INLINE static inline __attribute__((always_inline))

namespace recomp {

struct CPU {
    uint32_t r[16];       // r13 = sp, r14 = lr; r15: PC is a constant in generated code, r[15] only
                          // carries return targets of Function::pops_caller_frame callees
    uint8_t n, z, c, v;   // APSR flags (0/1)
    uint8_t q;            // sticky saturation
    uint8_t fp_n, fp_z, fp_c, fp_v;  // FPSCR flags (from VCMP)
    uint32_t ge;          // APSR.GE (4 bits)
    uint32_t fpscr;       // control bits only (rounding mode etc.)
    alignas(16) uint64_t d[32];      // VFP/NEON registers; s(2n), s(2n+1) alias d(n); q(n) = d(2n):d(2n+1)

    // Exclusive monitor for LDREX/STREX (emulated with compare-and-swap).
    uint32_t excl_addr;
    uint64_t excl_value;
    uint8_t excl_valid;

    uint32_t tls;         // TPIDRURO (read via mrc p15, 0, rX, c13, c0, 3)
    void* host_thread;    // runtime-private per-thread data
};

using GuestFunc = void (*)(CPU*);

extern uint8_t* g_mem;

// ---------------------------------------------------------------- memory
RECOMP_INLINE uint8_t* host_ptr(uint32_t a) { return g_mem + a; }

template <typename T>
RECOMP_INLINE T ld(uint32_t a) {
    T v;
    std::memcpy(&v, g_mem + a, sizeof(T));
    return v;
}
template <typename T>
RECOMP_INLINE void st(uint32_t a, T v) {
    std::memcpy(g_mem + a, &v, sizeof(T));
}
RECOMP_INLINE uint32_t ld32(uint32_t a) { return ld<uint32_t>(a); }
RECOMP_INLINE uint32_t ld16(uint32_t a) { return ld<uint16_t>(a); }
RECOMP_INLINE uint32_t ld8(uint32_t a) { return ld<uint8_t>(a); }
RECOMP_INLINE uint32_t lds16(uint32_t a) { return uint32_t(int32_t(ld<int16_t>(a))); }
RECOMP_INLINE uint32_t lds8(uint32_t a) { return uint32_t(int32_t(ld<int8_t>(a))); }
RECOMP_INLINE uint64_t ld64(uint32_t a) { return ld<uint64_t>(a); }
RECOMP_INLINE void st32(uint32_t a, uint32_t v) { st<uint32_t>(a, v); }
RECOMP_INLINE void st16(uint32_t a, uint32_t v) { st<uint16_t>(a, uint16_t(v)); }
RECOMP_INLINE void st8(uint32_t a, uint32_t v) { st<uint8_t>(a, uint8_t(v)); }
RECOMP_INLINE void st64(uint32_t a, uint64_t v) { st<uint64_t>(a, v); }

// ---------------------------------------------------------------- flags
RECOMP_INLINE void set_nz(CPU* c, uint32_t r) {
    c->n = uint8_t(r >> 31);
    c->z = r == 0;
}
// AddWithCarry(a, b, cin) with NZCV update.
RECOMP_INLINE uint32_t adc_flags(CPU* c, uint32_t a, uint32_t b, uint32_t cin) {
    const uint64_t u = uint64_t(a) + b + cin;
    const uint32_t r = uint32_t(u);
    c->n = uint8_t(r >> 31);
    c->z = r == 0;
    c->c = uint8_t(u >> 32);
    c->v = uint8_t(((a ^ r) & (b ^ r)) >> 31);
    return r;
}
RECOMP_INLINE uint32_t adds(CPU* c, uint32_t a, uint32_t b) { return adc_flags(c, a, b, 0); }
RECOMP_INLINE uint32_t subs(CPU* c, uint32_t a, uint32_t b) { return adc_flags(c, a, ~b, 1); }

// Condition codes.
RECOMP_INLINE bool cond_hi(const CPU* c) { return c->c && !c->z; }
RECOMP_INLINE bool cond_ls(const CPU* c) { return !c->c || c->z; }
RECOMP_INLINE bool cond_ge(const CPU* c) { return c->n == c->v; }
RECOMP_INLINE bool cond_lt(const CPU* c) { return c->n != c->v; }
RECOMP_INLINE bool cond_gt(const CPU* c) { return !c->z && c->n == c->v; }
RECOMP_INLINE bool cond_le(const CPU* c) { return c->z || c->n != c->v; }

// ---------------------------------------------------------------- shifter
RECOMP_INLINE uint32_t ror32(uint32_t v, uint32_t n) { return std::rotr(v, int(n & 31)); }

// Register-specified shifts (amount = Rs[7:0]) with carry-out.
RECOMP_INLINE uint32_t lsl_c(uint32_t v, uint32_t n, uint8_t& c) {
    n &= 0xff;
    if (n == 0) return v;
    if (n < 32) { c = (v >> (32 - n)) & 1; return v << n; }
    c = n == 32 ? (v & 1) : 0;
    return 0;
}
RECOMP_INLINE uint32_t lsr_c(uint32_t v, uint32_t n, uint8_t& c) {
    n &= 0xff;
    if (n == 0) return v;
    if (n < 32) { c = (v >> (n - 1)) & 1; return v >> n; }
    c = n == 32 ? (v >> 31) : 0;
    return 0;
}
RECOMP_INLINE uint32_t asr_c(uint32_t v, uint32_t n, uint8_t& c) {
    n &= 0xff;
    if (n == 0) return v;
    if (n < 32) { c = (uint32_t(int32_t(v) >> (n - 1))) & 1; return uint32_t(int32_t(v) >> n); }
    c = v >> 31;
    return uint32_t(int32_t(v) >> 31);
}
RECOMP_INLINE uint32_t ror_c(uint32_t v, uint32_t n, uint8_t& c) {
    n &= 0xff;
    if (n == 0) return v;
    const uint32_t r = ror32(v, n);
    c = r >> 31;
    return r;
}
RECOMP_INLINE uint32_t lsl_reg(uint32_t v, uint32_t n) { n &= 0xff; return n < 32 ? v << n : 0; }
RECOMP_INLINE uint32_t lsr_reg(uint32_t v, uint32_t n) { n &= 0xff; return n < 32 ? v >> n : 0; }
RECOMP_INLINE uint32_t asr_reg(uint32_t v, uint32_t n) {
    n &= 0xff;
    return uint32_t(int32_t(v) >> (n < 32 ? n : 31));
}

// ---------------------------------------------------------------- misc ALU
RECOMP_INLINE uint32_t clz32(uint32_t v) { return v ? uint32_t(std::countl_zero(v)) : 32; }
RECOMP_INLINE uint32_t rbit32(uint32_t v) {
    v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1);
    v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
    v = ((v >> 4) & 0x0f0f0f0fu) | ((v & 0x0f0f0f0fu) << 4);
    return __builtin_bswap32(v);
}
RECOMP_INLINE uint32_t ssat32(CPU* c, int64_t v, unsigned bits) {
    const int64_t hi = (int64_t(1) << (bits - 1)) - 1, lo = -(int64_t(1) << (bits - 1));
    if (v > hi) { c->q = 1; return uint32_t(hi); }
    if (v < lo) { c->q = 1; return uint32_t(lo); }
    return uint32_t(v);
}
RECOMP_INLINE uint32_t usat32(CPU* c, int64_t v, unsigned bits) {
    const int64_t hi = (int64_t(1) << bits) - 1;
    if (v > hi) { c->q = 1; return uint32_t(hi); }
    if (v < 0) { c->q = 1; return 0; }
    return uint32_t(v);
}
RECOMP_INLINE uint32_t qadd32(CPU* c, uint32_t a, uint32_t b) { return ssat32(c, int64_t(int32_t(a)) + int32_t(b), 32); }
RECOMP_INLINE uint32_t qsub32(CPU* c, uint32_t a, uint32_t b) { return ssat32(c, int64_t(int32_t(a)) - int32_t(b), 32); }
RECOMP_INLINE uint32_t udiv32(uint32_t a, uint32_t b) { return b ? a / b : 0; }
RECOMP_INLINE uint32_t sdiv32(uint32_t a, uint32_t b) {
    if (b == 0) return 0;
    if (a == 0x80000000u && b == 0xffffffffu) return a;
    return uint32_t(int32_t(a) / int32_t(b));
}

// ---------------------------------------------------------------- VFP
RECOMP_INLINE uint32_t s_get(const CPU* c, unsigned i) { return uint32_t(c->d[i >> 1] >> ((i & 1) * 32)); }
RECOMP_INLINE void s_set(CPU* c, unsigned i, uint32_t v) {
    const unsigned sh = (i & 1) * 32;
    c->d[i >> 1] = (c->d[i >> 1] & ~(uint64_t(0xffffffffu) << sh)) | (uint64_t(v) << sh);
}
RECOMP_INLINE float f32(uint32_t bits) { return std::bit_cast<float>(bits); }
RECOMP_INLINE uint32_t b32(float f) { return std::bit_cast<uint32_t>(f); }
RECOMP_INLINE double f64(uint64_t bits) { return std::bit_cast<double>(bits); }
RECOMP_INLINE uint64_t b64(double f) { return std::bit_cast<uint64_t>(f); }

// VCMP: sets FPSCR.NZCV.
template <typename F>
RECOMP_INLINE void vcmp(CPU* c, F a, F b) {
    if (std::isnan(a) || std::isnan(b)) { c->fp_n = 0; c->fp_z = 0; c->fp_c = 1; c->fp_v = 1; }
    else if (a == b) { c->fp_n = 0; c->fp_z = 1; c->fp_c = 1; c->fp_v = 0; }
    else if (a < b) { c->fp_n = 1; c->fp_z = 0; c->fp_c = 0; c->fp_v = 0; }
    else { c->fp_n = 0; c->fp_z = 0; c->fp_c = 1; c->fp_v = 0; }
}
RECOMP_INLINE void vmrs_apsr(CPU* c) { c->n = c->fp_n; c->z = c->fp_z; c->c = c->fp_c; c->v = c->fp_v; }

// Saturating float -> integer conversions (ARM semantics: NaN -> 0).
template <typename F>
RECOMP_INLINE uint32_t cvt_s32(F x) {
    if (std::isnan(x)) return 0;
    if (x >= F(2147483648.0)) return 0x7fffffffu;
    if (x <= F(-2147483648.0)) return 0x80000000u;
    return uint32_t(int32_t(x));
}
template <typename F>
RECOMP_INLINE uint32_t cvt_u32(F x) {
    if (std::isnan(x) || x <= F(0)) return 0;
    if (x >= F(4294967296.0)) return 0xffffffffu;
    return uint32_t(x);
}
// Round per FPSCR mode (VCVTR); only round-to-nearest is used in practice.
template <typename F>
RECOMP_INLINE uint32_t cvtr_s32(F x) { return cvt_s32(std::nearbyint(x)); }
template <typename F>
RECOMP_INLINE uint32_t cvtr_u32(F x) { return cvt_u32(std::nearbyint(x)); }

// ---------------------------------------------------------------- runtime entry points
// Implemented in the runtime, called by generated code.
void call_indirect(CPU* c, uint32_t target);         // BLX reg
void jump_indirect(CPU* c, uint32_t target);         // computed jump out of the function
void call_import(CPU* c, uint32_t index);            // call through the import table
void svc(CPU* c, uint32_t imm);                      // SVC #imm
void mrc(CPU* c, uint32_t pc, uint32_t rt, uint32_t enc);  // coprocessor read
[[noreturn]] void trap(CPU* c, uint32_t pc, const char* what);
void unimplemented(CPU* c, uint32_t pc, const char* text);
uint32_t strex32(CPU* c, uint32_t addr, uint32_t value);
uint32_t strex16(CPU* c, uint32_t addr, uint32_t value);
uint32_t strex8(CPU* c, uint32_t addr, uint32_t value);
uint32_t strexd(CPU* c, uint32_t addr, uint64_t value);
uint32_t ldrex32(CPU* c, uint32_t addr);
uint32_t ldrex16(CPU* c, uint32_t addr);
uint32_t ldrex8(CPU* c, uint32_t addr);
uint64_t ldrexd(CPU* c, uint32_t addr);
void clrex(CPU* c);
void barrier();

// Runtime replacement for a guest function (e.g. the unwinder's final jump).
GuestFunc find_hook(const char* name);
// The guest's own code behind a hook that wraps it (abrecomp emit --wrap), or
// nullptr if the generated code has no such wrapped function.
GuestFunc find_original(const char* hook);

// Non-local jump to `pc` in the guest frame whose stack pointer is `sp`
// (C++ exception landing pads, longjmp). Thrown as a host exception: the
// generated function owning that frame catches it and resumes at `pc`. The
// guest registers are already loaded into the CPU when it is thrown.
struct GuestUnwind {
    uint32_t pc;  // without the Thumb bit
    uint32_t sp;
};

}  // namespace recomp
