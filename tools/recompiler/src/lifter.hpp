// SPDX-License-Identifier: GPL-3.0-or-later
// Translates single ARM/Thumb instructions into C++ statements operating on
// recomp::CPU (see runtime/core/include/recomp/cpu.hpp).
#pragma once

#include <cstdint>
#include <string>

#include "disasm.hpp"
#include "elf_image.hpp"

namespace recomp {

struct LiftResult {
    std::string code;     // statements (no condition wrapper, no control transfer)
    std::string new_pc;   // if the instruction writes PC: name of the variable holding the target
    bool unimplemented = false;
};

class Lifter {
public:
    explicit Lifter(const ElfImage& image) : image_(image) {}

    LiftResult lift(const Insn& in);

    // Shared helpers (also used by the emitter).
    static int gpr(unsigned reg);  // 0..15 or -1
    static std::string hex(uint32_t v);
    std::string cond_expr(unsigned cc) const;

private:
    // Expressions.
    std::string read(const Insn& in, unsigned reg, bool align_pc = false) const;
    std::string reg_lv(unsigned reg) const;
    std::string shifted(const Insn& in, const cs_arm_op& o, bool want_carry, std::string& pre,
                        std::string& carry, int& tmp) const;
    std::string imm_carry(const Insn& in, uint32_t imm) const;
    bool literal(uint32_t addr, unsigned size, uint64_t& value) const;

    // Instruction classes.
    bool lift_data_processing(const Insn& in, LiftResult& out, int& tmp);
    bool lift_shift(const Insn& in, LiftResult& out, int& tmp);
    bool lift_multiply(const Insn& in, LiftResult& out);
    bool lift_bits(const Insn& in, LiftResult& out, int& tmp);
    bool lift_load_store(const Insn& in, LiftResult& out, int& tmp);
    bool lift_multiple(const Insn& in, LiftResult& out);
    bool lift_exclusive(const Insn& in, LiftResult& out);
    bool lift_misc(const Insn& in, LiftResult& out);
    bool lift_vfp(const Insn& in, LiftResult& out);
    bool lift_vfp_load_store(const Insn& in, LiftResult& out);
    bool lift_vfp_multiple(const Insn& in, LiftResult& out);

    const ElfImage& image_;
};

}  // namespace recomp
