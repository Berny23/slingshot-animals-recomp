// SPDX-License-Identifier: GPL-3.0-or-later
#include "nx_elf.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace apk {
namespace {

template <typename T>
T get(const std::vector<uint8_t>& d, size_t at) {
    T v;
    std::memcpy(&v, d.data() + at, sizeof v);  // ELF and host: little-endian
    return v;
}

constexpr uint32_t kRtMask = ~uint32_t(31);
constexpr uint32_t kMrsGcspr = 0xd53b2520;  // mrs Xt, GCSPR_EL0 (S3_3_C2_C5_1)
constexpr uint32_t kGcspopm = 0xd52b7720;   // gcspopm Xt (sysl Xt, #3, c7, c7, #1)
constexpr uint32_t kMovZero = 0xd2800000;   // movz Xt, #0
constexpr uint32_t kNop = 0xd503201f;

}  // namespace

int patch_switch_elf(const std::string& in, const std::string& out, std::string& error) {
    std::ifstream f(in, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 64 || std::memcmp(d.data(), "\x7f" "ELF\x02\x01", 6) != 0 || get<uint16_t>(d, 18) != 183) {
        error = in + ": not a little-endian AArch64 ELF64 file";
        return -1;
    }
    const uint64_t phoff = get<uint64_t>(d, 32);
    const uint16_t phentsize = get<uint16_t>(d, 54), phnum = get<uint16_t>(d, 56);
    if (phentsize < 56 || phoff + uint64_t(phentsize) * phnum > d.size()) {
        error = in + ": bad program headers";
        return -1;
    }
    int changed = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + size_t(i) * phentsize;
        if (get<uint32_t>(d, ph) != 1 /* PT_LOAD */ || !(get<uint32_t>(d, ph + 4) & 1 /* PF_X */)) continue;
        const uint64_t off = get<uint64_t>(d, ph + 8), size = get<uint64_t>(d, ph + 32);
        if (off > d.size() || size > d.size() - off) {
            error = in + ": segment outside the file";
            return -1;
        }
        for (uint64_t at = (off + 3) & ~uint64_t(3); at + 4 <= off + size; at += 4) {
            const uint32_t w = get<uint32_t>(d, at);
            if ((w & kRtMask) != kMrsGcspr && (w & kRtMask) != kGcspopm) continue;
            const uint32_t rt = w & 31;
            const uint32_t repl = rt == 31 ? kNop : kMovZero | rt;
            std::memcpy(d.data() + at, &repl, 4);
            ++changed;
        }
    }
    std::ofstream o(out, std::ios::binary);
    if (!o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()))) {
        error = "cannot write " + out;
        return -1;
    }
    return changed;
}

}  // namespace apk
