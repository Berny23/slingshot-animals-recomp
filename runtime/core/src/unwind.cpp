// SPDX-License-Identifier: GPL-3.0-or-later
// Support for the guest's C++ exception handling.
//
// The game's own unwinder (libgcc's ARM EHABI implementation) is recompiled
// and works on the guest stack as usual. Its last step, restore_core_regs,
// loads all registers and jumps into a landing pad; that jump is replaced by
// a GuestUnwind exception that the generated function owning the target frame
// catches (see the emitter).
#include <cstdio>
#include <cstring>

#include "recomp/generated.hpp"
#include "recomp/hle.hpp"
#include "recomp/runtime.hpp"

namespace recomp {

namespace {

void missing_hook(CPU* c) { trap(c, 0, "unimplemented hook"); }

// restore_core_regs(struct core_regs* r0): r0..r15 in order.
void restore_core_regs(CPU* c) {
    const uint32_t regs = c->r[0];
    uint32_t v[16];
    for (int i = 0; i < 16; ++i) v[i] = ld32(regs + 4 * uint32_t(i));
    for (int i = 0; i < 15; ++i) c->r[i] = v[i];
    if (trace_enabled("unwind")) std::fprintf(stderr, "unwind: resume at 0x%08x sp=0x%08x\n", v[15], v[13]);
    throw GuestUnwind{v[15] & ~1u, v[13]};
}

RECOMP_REGISTER_IMPORT("hook:restore_core_regs", restore_core_regs);

}  // namespace

GuestFunc find_hook(const char* name) {
    const std::string key = std::string("hook:") + name;
    if (hle::HostFn f = hle::find_import(key.c_str())) return f;
    std::fprintf(stderr, "recomp: no runtime hook %s\n", name);
    return missing_hook;
}

GuestFunc find_original(const char* hook) {
    for (size_t i = 0; i < gen::original_count; ++i)
        if (!std::strcmp(gen::originals[i].hook, hook)) return gen::originals[i].fn;
    return nullptr;
}

}  // namespace recomp
