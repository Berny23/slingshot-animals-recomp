// SPDX-License-Identifier: GPL-3.0-or-later
// Thin capstone wrapper with a per-(address, mode) decode cache.
#pragma once

#include <capstone/capstone.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "elf_image.hpp"

namespace recomp {

enum class Mode : uint8_t { Arm = 0, Thumb = 1 };

struct Insn {
    uint32_t addr = 0;
    uint8_t size = 0;
    Mode mode = Mode::Arm;
    unsigned id = 0;  // ARM_INS_*
    const char* name = "";  // canonical instruction name (no condition suffix)
    std::string mnemonic;
    std::string op_str;
    cs_arm arm{};     // operand detail
    uint16_t regs_read[20]{};
    uint8_t regs_read_count = 0;
    uint16_t regs_write[20]{};
    uint8_t regs_write_count = 0;
    uint8_t groups[8]{};
    uint8_t groups_count = 0;
    uint32_t raw = 0;  // raw encoding (Thumb-2 32-bit: hw1 << 16 | hw2)

    bool in_group(unsigned g) const {
        for (uint8_t i = 0; i < groups_count; ++i)
            if (groups[i] == g) return true;
        return false;
    }
    bool writes_reg(unsigned r) const {
        for (uint8_t i = 0; i < regs_write_count; ++i)
            if (regs_write[i] == r) return true;
        return false;
    }
    std::string text() const { return mnemonic + (op_str.empty() ? "" : " " + op_str); }
};

class Disassembler {
public:
    explicit Disassembler(const ElfImage& image);
    ~Disassembler();
    Disassembler(const Disassembler&) = delete;
    Disassembler& operator=(const Disassembler&) = delete;

    // Returns nullptr if the bytes at addr do not decode in this mode.
    const Insn* decode(uint32_t addr, Mode mode);

private:
    const Insn* decode_uncached(uint32_t addr, Mode mode);
    void reset_thumb();

    const ElfImage& image_;
    csh arm_ = 0, thumb_ = 0;
    cs_insn* scratch_arm_ = nullptr;
    cs_insn* scratch_thumb_ = nullptr;
    std::unordered_map<uint64_t, std::unique_ptr<Insn>> cache_;
};

}  // namespace recomp
