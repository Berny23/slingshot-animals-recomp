// SPDX-License-Identifier: GPL-3.0-or-later
// High-level emulation (HLE) of imported functions.
//
// Imports are host functions with the guest calling convention
// `void (CPU*)`. Most are written as typed C++ functions and adapted with
// RECOMP_HLE(fn), which reads the AAPCS (softfp) arguments and stores the
// return value:
//
//     static uint32_t my_strlen(uint32_t s) { return std::strlen(mem::str(s)); }
//     RECOMP_REGISTER_IMPORT("strlen", RECOMP_HLE(my_strlen));
//
// Guest pointers are plain uint32_t guest addresses.
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>

#include "recomp/cpu.hpp"

namespace recomp::hle {

using HostFn = void (*)(CPU*);

void register_import(const char* name, HostFn fn);
// Data symbol imported by the game (e.g. "__sF"); value is a guest address.
void register_data(const char* name, uint32_t guest_addr);
HostFn find_import(const char* name);
// Runs after guest memory exists and before the library is linked (for
// modules that need to allocate guest data, e.g. to register data imports).
void register_init_hook(void (*fn)());
void run_init_hooks();
bool find_data(const char* name, uint32_t& addr);

// Sequential reader for AAPCS arguments: r0-r3, then the stack.
class ArgReader {
public:
    explicit ArgReader(CPU* c) : c_(c), sp_(c->r[13]) {}
    // va_list style reader over guest memory.
    static ArgReader from_memory(CPU* c, uint32_t addr) {
        ArgReader r(c);
        r.ncrn_ = 4;
        r.sp_ = addr;
        return r;
    }

    uint32_t u32() {
        if (ncrn_ < 4) return c_->r[ncrn_++];
        const uint32_t v = ld32(sp_);
        sp_ += 4;
        return v;
    }
    uint64_t u64() {
        ncrn_ = (ncrn_ + 1) & ~1u;
        if (ncrn_ <= 2) {
            const uint64_t v = uint64_t(c_->r[ncrn_]) | uint64_t(c_->r[ncrn_ + 1]) << 32;
            ncrn_ += 2;
            return v;
        }
        ncrn_ = 4;
        sp_ = (sp_ + 7) & ~7u;
        const uint64_t v = ld64(sp_);
        sp_ += 8;
        return v;
    }
    double f64() { return std::bit_cast<double>(u64()); }
    float f32() { return std::bit_cast<float>(u32()); }
    uint32_t stack_pointer() const { return sp_; }

    template <typename T>
    T get() {
        if constexpr (std::is_same_v<T, CPU*>) return c_;
        else if constexpr (std::is_same_v<T, ArgReader>) return *this;  // varargs: remaining arguments
        else if constexpr (std::is_same_v<T, double>) return f64();
        else if constexpr (std::is_same_v<T, float>) return f32();
        else if constexpr (sizeof(T) == 8) return T(u64());
        else return T(u32());
    }

private:
    CPU* c_;
    unsigned ncrn_ = 0;
    uint32_t sp_;
};

template <typename T>
void set_return(CPU* c, T v) {
    if constexpr (std::is_same_v<T, double>) {
        const uint64_t b = std::bit_cast<uint64_t>(v);
        c->r[0] = uint32_t(b);
        c->r[1] = uint32_t(b >> 32);
    } else if constexpr (std::is_same_v<T, float>) {
        c->r[0] = std::bit_cast<uint32_t>(v);
    } else if constexpr (sizeof(T) == 8) {
        c->r[0] = uint32_t(uint64_t(v));
        c->r[1] = uint32_t(uint64_t(v) >> 32);
    } else {
        c->r[0] = uint32_t(v);
    }
}

template <typename F>
struct Adapter;

template <typename R, typename... A>
struct Adapter<R (*)(A...)> {
    template <R (*F)(A...)>
    static void call(CPU* c) {
        ArgReader rd(c);
        // Braced initialisation evaluates the reads left to right.
        std::tuple<A...> args{rd.get<A>()...};
        if constexpr (std::is_void_v<R>) std::apply(F, args);
        else set_return(c, std::apply(F, args));
    }
};

#define RECOMP_HLE(fn) (&::recomp::hle::Adapter<decltype(&fn)>::template call<&fn>)

// Static registration helper: RECOMP_REGISTER_IMPORT("name", fn) at namespace scope.
struct Registrar {
    Registrar(const char* name, HostFn fn) { register_import(name, fn); }
};
#define RECOMP_CONCAT2(a, b) a##b
#define RECOMP_CONCAT(a, b) RECOMP_CONCAT2(a, b)
#define RECOMP_REGISTER_IMPORT(name, fn) \
    static ::recomp::hle::Registrar RECOMP_CONCAT(recomp_reg_, __COUNTER__)(name, fn)
struct InitRegistrar {
    explicit InitRegistrar(void (*fn)()) { register_init_hook(fn); }
};
#define RECOMP_INIT_HOOK(fn) static ::recomp::hle::InitRegistrar RECOMP_CONCAT(recomp_init_, __COUNTER__)(fn)

}  // namespace recomp::hle
