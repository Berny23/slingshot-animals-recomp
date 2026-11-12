// SPDX-License-Identifier: GPL-3.0-or-later
// Data objects imported from bionic: ctype tables and the stack guard.
#include <cctype>
#include <cstring>

#include "libc.hpp"
#include "recomp/memory.hpp"
#include "runtime_internal.hpp"

namespace recomp::hle {

uint32_t internal_import_thunk(const char* name) { return internal::import_thunk(name); }

namespace {

void init_data() {
    // bionic: extern const char* _ctype_;  (table of 1 + 256 entries, index 0 is EOF)
    enum : uint8_t { U = 0x01, L = 0x02, N = 0x04, S = 0x08, P = 0x10, C = 0x20, X = 0x40, B = 0x80 };
    const uint32_t ctype = mem::static_alloc(257, 4);
    g_mem[ctype] = 0;
    for (int ch = 0; ch < 256; ++ch) {
        uint8_t f = 0;
        if (ch < 128) {
            if (std::isupper(ch)) f |= U;
            if (std::islower(ch)) f |= L;
            if (std::isdigit(ch)) f |= N;
            if (std::isspace(ch)) f |= S;
            if (std::ispunct(ch)) f |= P;
            if (std::iscntrl(ch)) f |= C;
            if (std::isxdigit(ch) && !std::isdigit(ch)) f |= X;
            if (ch == ' ') f |= B;
        }
        g_mem[ctype + 1 + ch] = f;
    }
    const uint32_t ctype_ptr = mem::static_alloc(4, 4);
    st32(ctype_ptr, ctype);
    register_data("_ctype_", ctype_ptr);

    // extern const short* _tolower_tab_ / _toupper_tab_;  (1 + 256 shorts, index 0 is EOF)
    for (int upper = 0; upper < 2; ++upper) {
        const uint32_t tab = mem::static_alloc(257 * 2, 4);
        st16(tab, 0xffff);
        for (int ch = 0; ch < 256; ++ch)
            st16(tab + 2 + 2 * ch, uint32_t(ch < 128 ? (upper ? std::toupper(ch) : std::tolower(ch)) : ch));
        const uint32_t ptr = mem::static_alloc(4, 4);
        st32(ptr, tab);
        register_data(upper ? "_toupper_tab_" : "_tolower_tab_", ptr);
    }

    const uint32_t guard = mem::static_alloc(4, 4);
    st32(guard, 0x6d1a52c3u);
    register_data("__stack_chk_guard", guard);
}
RECOMP_INIT_HOOK(init_data);

}  // namespace
}  // namespace recomp::hle
