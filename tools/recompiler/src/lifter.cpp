// SPDX-License-Identifier: GPL-3.0-or-later
#include "lifter.hpp"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace recomp {
namespace {

const cs_arm_op* op(const Insn& in, unsigned i) { return i < in.arm.op_count ? &in.arm.operands[i] : nullptr; }
bool is_reg(const cs_arm_op* o) { return o && o->type == ARM_OP_REG; }
bool is_imm(const cs_arm_op* o) { return o && (o->type == ARM_OP_IMM || o->type == ARM_OP_PIMM || o->type == ARM_OP_CIMM); }
bool is_mem(const cs_arm_op* o) { return o && o->type == ARM_OP_MEM; }
uint32_t pc_of(const Insn& in) { return in.addr + (in.mode == Mode::Thumb ? 4 : 8); }
bool is_cond(const Insn& in) { return in.arm.cc != ARM_CC_AL && in.arm.cc != ARM_CC_INVALID; }

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string tmpname(int& tmp) { return "t" + std::to_string(tmp++); }


}  // namespace

int Lifter::gpr(unsigned reg) {
    if (reg >= ARM_REG_R0 && reg <= ARM_REG_R12) return int(reg - ARM_REG_R0);
    if (reg == ARM_REG_SP) return 13;
    if (reg == ARM_REG_LR) return 14;
    if (reg == ARM_REG_PC) return 15;
    return -1;
}

std::string Lifter::hex(uint32_t v) { return fmt("0x%xu", v); }

std::string Lifter::cond_expr(unsigned cc) const {
    switch (cc) {
        case ARM_CC_EQ: return "ctx->z";
        case ARM_CC_NE: return "!ctx->z";
        case ARM_CC_HS: return "ctx->c";
        case ARM_CC_LO: return "!ctx->c";
        case ARM_CC_MI: return "ctx->n";
        case ARM_CC_PL: return "!ctx->n";
        case ARM_CC_VS: return "ctx->v";
        case ARM_CC_VC: return "!ctx->v";
        case ARM_CC_HI: return "recomp::cond_hi(ctx)";
        case ARM_CC_LS: return "recomp::cond_ls(ctx)";
        case ARM_CC_GE: return "recomp::cond_ge(ctx)";
        case ARM_CC_LT: return "recomp::cond_lt(ctx)";
        case ARM_CC_GT: return "recomp::cond_gt(ctx)";
        case ARM_CC_LE: return "recomp::cond_le(ctx)";
        default: return "true";
    }
}

std::string Lifter::read(const Insn& in, unsigned reg, bool align_pc) const {
    const int r = gpr(reg);
    if (r == 15) return hex(align_pc ? pc_of(in) & ~3u : pc_of(in));
    if (r >= 0) return fmt("ctx->r[%d]", r);
    return "/*bad reg*/0u";
}

std::string Lifter::reg_lv(unsigned reg) const { return fmt("ctx->r[%d]", gpr(reg)); }

// Read-only image data (literal pools) can be folded at recompile time.
bool Lifter::literal(uint32_t addr, unsigned size, uint64_t& value) const {
    for (const auto& s : image_.segments())
        if (s.type == 1 && !(s.flags & 2) && addr >= s.vaddr && addr + size <= s.vaddr + s.filesz) {
            value = size == 8 ? (uint64_t(image_.read32(addr + 4)) << 32 | image_.read32(addr))
                  : size == 4 ? image_.read32(addr)
                  : size == 2 ? image_.read16(addr) : image_.read8(addr);
            return true;
        }
    return false;
}

// Carry-out of a modified-immediate operand: unchanged unless the encoding rotates.
std::string Lifter::imm_carry(const Insn& in, uint32_t imm) const {
    bool rotated = false;
    if (in.mode == Mode::Arm) {
        rotated = ((in.raw >> 8) & 0xf) != 0;
    } else if (in.size == 4) {
        const uint32_t hw1 = in.raw >> 16, hw2 = in.raw & 0xffff;
        const uint32_t top = ((hw1 >> 10) & 1) << 1 | ((hw2 >> 14) & 1);  // imm12[11:10]
        rotated = top != 0;
    }
    return rotated ? hex(imm >> 31) : std::string();
}

std::string Lifter::shifted(const Insn& in, const cs_arm_op& o, bool want_carry, std::string& pre,
                            std::string& carry, int& tmp) const {
    const std::string v = read(in, o.reg);
    const unsigned n = o.shift.value;
    carry.clear();
    switch (o.shift.type) {
        case ARM_SFT_INVALID:
            return v;
        case ARM_SFT_LSL:
            if (n == 0) return v;
            if (want_carry) carry = fmt("((%s >> %u) & 1)", v.c_str(), 32 - n);
            return fmt("(%s << %u)", v.c_str(), n);
        case ARM_SFT_LSR:
            if (want_carry) carry = n == 32 ? fmt("(%s >> 31)", v.c_str()) : fmt("((%s >> %u) & 1)", v.c_str(), n - 1);
            return n == 32 ? std::string("0u") : fmt("(%s >> %u)", v.c_str(), n);
        case ARM_SFT_ASR:
            if (want_carry)
                carry = n == 32 ? fmt("(%s >> 31)", v.c_str()) : fmt("((%s >> %u) & 1)", v.c_str(), n - 1);
            return fmt("uint32_t(int32_t(%s) >> %u)", v.c_str(), n == 32 ? 31 : n);
        case ARM_SFT_ROR:
            if (want_carry) carry = fmt("((%s >> %u) & 1)", v.c_str(), n - 1);
            return fmt("recomp::ror32(%s, %u)", v.c_str(), n);
        case ARM_SFT_RRX:
            if (want_carry) carry = fmt("(%s & 1)", v.c_str());
            return fmt("((uint32_t(ctx->c) << 31) | (%s >> 1))", v.c_str());
        case ARM_SFT_LSL_REG:
        case ARM_SFT_LSR_REG:
        case ARM_SFT_ASR_REG:
        case ARM_SFT_ROR_REG: {
            const std::string rs = read(in, n);
            const char* base = o.shift.type == ARM_SFT_LSL_REG ? "lsl" : o.shift.type == ARM_SFT_LSR_REG ? "lsr"
                             : o.shift.type == ARM_SFT_ASR_REG ? "asr" : "ror";
            if (want_carry) {
                const std::string c = tmpname(tmp), r = tmpname(tmp);
                pre += fmt("uint8_t %s = ctx->c; const uint32_t %s = recomp::%s_c(%s, %s, %s); ", c.c_str(), r.c_str(),
                           base, v.c_str(), rs.c_str(), c.c_str());
                carry = c;
                return r;
            }
            if (o.shift.type == ARM_SFT_ROR_REG) return fmt("recomp::ror32(%s, %s)", v.c_str(), rs.c_str());
            return fmt("recomp::%s_reg(%s, %s)", base, v.c_str(), rs.c_str());
        }
        default:
            return "/*bad shift*/0u";
    }
}

// ------------------------------------------------------------------ data processing
bool Lifter::lift_data_processing(const Insn& in, LiftResult& out, int& tmp) {
    enum K { AND, EOR, ORR, BIC, ORN, MOV, MVN, TST, TEQ, ADD, ADC, SUB, SBC, RSB, RSC, CMP, CMN };
    K k;
    switch (in.id) {
        case ARM_INS_AND: k = AND; break;
        case ARM_INS_EOR: k = EOR; break;
        case ARM_INS_ORR: k = ORR; break;
        case ARM_INS_BIC: k = BIC; break;
        case ARM_INS_ORN: k = ORN; break;
        case ARM_INS_MOV: case ARM_INS_MOVW: k = MOV; break;
        case ARM_INS_MVN: k = MVN; break;
        case ARM_INS_TST: k = TST; break;
        case ARM_INS_TEQ: k = TEQ; break;
        case ARM_INS_ADD: case ARM_INS_ADDW: k = ADD; break;
        case ARM_INS_ADC: k = ADC; break;
        case ARM_INS_SUB: case ARM_INS_SUBW: k = SUB; break;
        case ARM_INS_SBC: k = SBC; break;
        case ARM_INS_RSB: k = RSB; break;
        case ARM_INS_RSC: k = RSC; break;
        case ARM_INS_CMP: k = CMP; break;
        case ARM_INS_CMN: k = CMN; break;
        default: return false;
    }
    const bool compare = k == TST || k == TEQ || k == CMP || k == CMN;
    const bool unary = k == MOV || k == MVN;
    const bool logical = k <= TEQ;
    const bool flags = compare || in.arm.update_flags;

    const cs_arm_op *d = nullptr, *n = nullptr, *m = nullptr;
    if (compare) { n = op(in, 0); m = op(in, 1); }
    else if (unary) { d = op(in, 0); m = op(in, 1); }
    else if (in.arm.op_count >= 3) { d = op(in, 0); n = op(in, 1); m = op(in, 2); }
    else { d = op(in, 0); n = op(in, 0); m = op(in, 1); }
    if (!m || (n && !is_reg(n)) || (d && !is_reg(d))) return false;

    std::string pre, carry, b;
    if (is_imm(m)) {
        const uint32_t imm = uint32_t(m->imm);
        b = hex(imm);
        if (flags && logical) carry = imm_carry(in, imm);
    } else if (is_reg(m)) {
        b = shifted(in, *m, flags && logical, pre, carry, tmp);
    } else {
        return false;
    }
    const std::string a = n ? read(in, n->reg, is_imm(m)) : std::string();
    std::string e;
    switch (k) {
        case AND: case TST: e = a + " & " + b; break;
        case EOR: case TEQ: e = a + " ^ " + b; break;
        case ORR: e = a + " | " + b; break;
        case BIC: e = a + " & ~" + b; break;
        case ORN: e = a + " | ~" + b; break;
        case MOV: e = b; break;
        case MVN: e = "~" + b; break;
        case ADD: e = flags ? fmt("recomp::adc_flags(ctx, %s, %s, 0)", a.c_str(), b.c_str()) : a + " + " + b; break;
        case CMN: e = fmt("recomp::adc_flags(ctx, %s, %s, 0)", a.c_str(), b.c_str()); break;
        case ADC: e = flags ? fmt("recomp::adc_flags(ctx, %s, %s, ctx->c)", a.c_str(), b.c_str())
                            : a + " + " + b + " + ctx->c"; break;
        case SUB: case CMP:
            e = flags ? fmt("recomp::adc_flags(ctx, %s, ~%s, 1)", a.c_str(), b.c_str()) : a + " - " + b; break;
        case SBC: e = flags ? fmt("recomp::adc_flags(ctx, %s, ~%s, ctx->c)", a.c_str(), b.c_str())
                            : a + " + ~" + b + " + ctx->c"; break;
        case RSB: e = flags ? fmt("recomp::adc_flags(ctx, %s, ~%s, 1)", b.c_str(), a.c_str()) : b + " - " + a; break;
        case RSC: e = flags ? fmt("recomp::adc_flags(ctx, %s, ~%s, ctx->c)", b.c_str(), a.c_str())
                            : b + " + ~" + a + " + ctx->c"; break;
    }
    const std::string t = tmpname(tmp);
    std::string code = pre + fmt("const uint32_t %s = %s; ", t.c_str(), e.c_str());
    if (flags && logical) {
        if (!carry.empty()) code += "ctx->c = uint8_t(" + carry + "); ";
        code += "recomp::set_nz(ctx, " + t + "); ";
    }
    if (d) {
        if (gpr(d->reg) == 15) out.new_pc = t;
        else code += reg_lv(d->reg) + " = " + t + "; ";
    }
    out.code += code;
    return true;
}

bool Lifter::lift_shift(const Insn& in, LiftResult& out, int& tmp) {
    arm_shifter imm_type, reg_type;
    switch (in.id) {
        case ARM_INS_LSL: imm_type = ARM_SFT_LSL; reg_type = ARM_SFT_LSL_REG; break;
        case ARM_INS_LSR: imm_type = ARM_SFT_LSR; reg_type = ARM_SFT_LSR_REG; break;
        case ARM_INS_ASR: imm_type = ARM_SFT_ASR; reg_type = ARM_SFT_ASR_REG; break;
        case ARM_INS_ROR: imm_type = ARM_SFT_ROR; reg_type = ARM_SFT_ROR_REG; break;
        case ARM_INS_RRX: imm_type = ARM_SFT_RRX; reg_type = ARM_SFT_RRX; break;
        default: return false;
    }
    const cs_arm_op* d = op(in, 0);
    if (!is_reg(d)) return false;
    cs_arm_op src{};
    if (in.arm.op_count == 2 && op(in, 1)->shift.type != ARM_SFT_INVALID) {
        src = *op(in, 1);                         // ARM: lsl rd, rm, #n  (shift on operand)
    } else if (in.arm.op_count == 2 && in.id == ARM_INS_RRX) {
        src = *op(in, 1);
        src.shift.type = ARM_SFT_RRX;
    } else if (in.arm.op_count == 2 && is_reg(op(in, 1))) {
        src = *d;                                 // Thumb: lsls rd, rs  (rd = rd << rs)
        src.shift.type = reg_type;
        src.shift.value = unsigned(op(in, 1)->reg);
    } else if (in.arm.op_count == 2 && is_imm(op(in, 1))) {
        src = *d;                                 // lsls rd, #n
        src.shift.type = imm_type;
        src.shift.value = unsigned(op(in, 1)->imm);
    } else if (in.arm.op_count == 3 && is_reg(op(in, 1))) {
        src = *op(in, 1);
        if (is_imm(op(in, 2))) {
            src.shift.type = imm_type;
            src.shift.value = unsigned(op(in, 2)->imm);
        } else if (is_reg(op(in, 2))) {
            src.shift.type = reg_type;
            src.shift.value = unsigned(op(in, 2)->reg);
        } else {
            return false;
        }
    } else {
        return false;
    }
    if (src.shift.type == ARM_SFT_ROR && src.shift.value == 0) return false;  // would be RRX
    const bool flags = in.arm.update_flags;
    std::string pre, carry;
    const std::string v = shifted(in, src, flags, pre, carry, tmp);
    const std::string t = tmpname(tmp);
    std::string code = pre + fmt("const uint32_t %s = %s; ", t.c_str(), v.c_str());
    if (flags) {
        if (!carry.empty()) code += "ctx->c = uint8_t(" + carry + "); ";
        code += "recomp::set_nz(ctx, " + t + "); ";
    }
    if (gpr(d->reg) == 15) out.new_pc = t;
    else code += reg_lv(d->reg) + " = " + t + "; ";
    out.code += code;
    return true;
}

// ------------------------------------------------------------------ multiply
bool Lifter::lift_multiply(const Insn& in, LiftResult& out) {
    auto R = [&](unsigned i) { return read(in, op(in, i)->reg); };
    auto W = [&](unsigned i) { return reg_lv(op(in, i)->reg); };
    for (unsigned i = 0; i < in.arm.op_count; ++i)
        if (!is_reg(op(in, i))) return false;
    const bool s = in.arm.update_flags;
    std::string c;
    auto half = [](const std::string& v, bool top) {
        return top ? fmt("int32_t(int16_t(%s >> 16))", v.c_str()) : fmt("int32_t(int16_t(%s))", v.c_str());
    };
    switch (in.id) {
        case ARM_INS_MUL: {
            // Thumb 2-operand form "muls rd, rm" multiplies rd by rm.
            const std::string a = in.arm.op_count == 2 ? R(0) : R(1), b = in.arm.op_count == 2 ? R(1) : R(2);
            c = fmt("const uint32_t m = %s * %s; ", a.c_str(), b.c_str());
            if (s) c += "recomp::set_nz(ctx, m); ";
            c += W(0) + " = m; ";
            break;
        }
        case ARM_INS_MLA:
            c = fmt("const uint32_t m = %s * %s + %s; ", R(1).c_str(), R(2).c_str(), R(3).c_str());
            if (s) c += "recomp::set_nz(ctx, m); ";
            c += W(0) + " = m; ";
            break;
        case ARM_INS_MLS:
            c = fmt("%s = %s - %s * %s; ", W(0).c_str(), R(3).c_str(), R(1).c_str(), R(2).c_str());
            break;
        case ARM_INS_UMULL: case ARM_INS_SMULL: case ARM_INS_UMLAL: case ARM_INS_SMLAL: case ARM_INS_UMAAL: {
            const bool sign = in.id == ARM_INS_SMULL || in.id == ARM_INS_SMLAL;
            const std::string a = sign ? "uint64_t(int64_t(int32_t(" + R(2) + ")))" : "uint64_t(" + R(2) + ")";
            const std::string b = sign ? "uint64_t(int64_t(int32_t(" + R(3) + ")))" : "uint64_t(" + R(3) + ")";
            c = fmt("uint64_t m = %s * %s; ", a.c_str(), b.c_str());
            if (in.id == ARM_INS_UMLAL || in.id == ARM_INS_SMLAL)
                c += fmt("m += (uint64_t(%s) << 32) | %s; ", R(1).c_str(), R(0).c_str());
            if (in.id == ARM_INS_UMAAL) c += fmt("m += uint64_t(%s) + uint64_t(%s); ", R(1).c_str(), R(0).c_str());
            if (s) c += "ctx->n = uint8_t(m >> 63); ctx->z = m == 0; ";
            c += W(0) + " = uint32_t(m); " + W(1) + " = uint32_t(m >> 32); ";
            break;
        }
        case ARM_INS_SMULBB: case ARM_INS_SMULBT: case ARM_INS_SMULTB: case ARM_INS_SMULTT: {
            const bool xt = in.id == ARM_INS_SMULTB || in.id == ARM_INS_SMULTT;
            const bool yt = in.id == ARM_INS_SMULBT || in.id == ARM_INS_SMULTT;
            c = fmt("%s = uint32_t(%s * %s); ", W(0).c_str(), half(R(1), xt).c_str(), half(R(2), yt).c_str());
            break;
        }
        case ARM_INS_SMLABB: case ARM_INS_SMLABT: case ARM_INS_SMLATB: case ARM_INS_SMLATT: {
            const bool xt = in.id == ARM_INS_SMLATB || in.id == ARM_INS_SMLATT;
            const bool yt = in.id == ARM_INS_SMLABT || in.id == ARM_INS_SMLATT;
            c = fmt("{ const int64_t m = int64_t(%s * %s) + int32_t(%s); if (m != int32_t(m)) ctx->q = 1; %s = uint32_t(m); } ",
                    half(R(1), xt).c_str(), half(R(2), yt).c_str(), R(3).c_str(), W(0).c_str());
            break;
        }
        case ARM_INS_SMULWB: case ARM_INS_SMULWT:
            c = fmt("%s = uint32_t((int64_t(int32_t(%s)) * %s) >> 16); ", W(0).c_str(), R(1).c_str(),
                    half(R(2), in.id == ARM_INS_SMULWT).c_str());
            break;
        case ARM_INS_SMLAWB: case ARM_INS_SMLAWT:
            c = fmt("{ const int64_t m = ((int64_t(int32_t(%s)) * %s) >> 16) + int32_t(%s); if (m != int32_t(m)) ctx->q = 1; %s = uint32_t(m); } ",
                    R(1).c_str(), half(R(2), in.id == ARM_INS_SMLAWT).c_str(), R(3).c_str(), W(0).c_str());
            break;
        case ARM_INS_SMLALBB: case ARM_INS_SMLALBT: case ARM_INS_SMLALTB: case ARM_INS_SMLALTT: {
            const bool xt = in.id == ARM_INS_SMLALTB || in.id == ARM_INS_SMLALTT;
            const bool yt = in.id == ARM_INS_SMLALBT || in.id == ARM_INS_SMLALTT;
            c = fmt("{ uint64_t m = (uint64_t(%s) << 32) | %s; m += uint64_t(int64_t(%s * %s)); %s = uint32_t(m); %s = uint32_t(m >> 32); } ",
                    R(1).c_str(), R(0).c_str(), half(R(2), xt).c_str(), half(R(3), yt).c_str(), W(0).c_str(), W(1).c_str());
            break;
        }
        case ARM_INS_SMMUL:
            c = fmt("%s = uint32_t(uint64_t(int64_t(int32_t(%s)) * int32_t(%s)) >> 32); ", W(0).c_str(), R(1).c_str(), R(2).c_str());
            break;
        case ARM_INS_SMMULR:
            c = fmt("%s = uint32_t((uint64_t(int64_t(int32_t(%s)) * int32_t(%s)) + 0x80000000ull) >> 32); ", W(0).c_str(),
                    R(1).c_str(), R(2).c_str());
            break;
        case ARM_INS_SMMLA:
            c = fmt("%s = uint32_t(((uint64_t(%s) << 32) + uint64_t(int64_t(int32_t(%s)) * int32_t(%s))) >> 32); ", W(0).c_str(),
                    R(3).c_str(), R(1).c_str(), R(2).c_str());
            break;
        case ARM_INS_SMMLS:
            c = fmt("%s = uint32_t(((uint64_t(%s) << 32) - uint64_t(int64_t(int32_t(%s)) * int32_t(%s))) >> 32); ", W(0).c_str(),
                    R(3).c_str(), R(1).c_str(), R(2).c_str());
            break;
        case ARM_INS_SMUAD:
            c = fmt("%s = uint32_t(%s * %s + %s * %s); ", W(0).c_str(), half(R(1), false).c_str(), half(R(2), false).c_str(),
                    half(R(1), true).c_str(), half(R(2), true).c_str());
            break;
        case ARM_INS_SMLAD:
            c = fmt("%s = uint32_t(%s * %s + %s * %s) + %s; ", W(0).c_str(), half(R(1), false).c_str(),
                    half(R(2), false).c_str(), half(R(1), true).c_str(), half(R(2), true).c_str(), R(3).c_str());
            break;
        case ARM_INS_SDIV:
            c = fmt("%s = recomp::sdiv32(%s, %s); ", W(0).c_str(), R(1).c_str(), R(2).c_str());
            break;
        case ARM_INS_UDIV:
            c = fmt("%s = recomp::udiv32(%s, %s); ", W(0).c_str(), R(1).c_str(), R(2).c_str());
            break;
        default:
            return false;
    }
    out.code += "{ " + c + "} ";
    return true;
}

// ------------------------------------------------------------------ bit manipulation / extension
bool Lifter::lift_bits(const Insn& in, LiftResult& out, int& tmp) {
    const cs_arm_op* d = op(in, 0);
    if (!is_reg(d)) return false;
    const std::string W = reg_lv(d->reg);
    auto R = [&](unsigned i) { return read(in, op(in, i)->reg); };
    auto rot = [&](unsigned i) {
        const cs_arm_op* o = op(in, i);
        const std::string v = read(in, o->reg);
        if (o->shift.type == ARM_SFT_ROR && o->shift.value) return fmt("recomp::ror32(%s, %u)", v.c_str(), o->shift.value);
        return v;
    };
    std::string c;
    switch (in.id) {
        case ARM_INS_CLZ: c = fmt("%s = recomp::clz32(%s);", W.c_str(), R(1).c_str()); break;
        case ARM_INS_RBIT: c = fmt("%s = recomp::rbit32(%s);", W.c_str(), R(1).c_str()); break;
        case ARM_INS_REV: c = fmt("%s = __builtin_bswap32(%s);", W.c_str(), R(1).c_str()); break;
        case ARM_INS_REV16:
            c = fmt("{ const uint32_t v = %s; %s = ((v & 0x00ff00ffu) << 8) | ((v >> 8) & 0x00ff00ffu); }", R(1).c_str(), W.c_str());
            break;
        case ARM_INS_REVSH:
            c = fmt("{ const uint32_t v = %s; %s = uint32_t(int32_t(int16_t(uint16_t(((v & 0xff) << 8) | ((v >> 8) & 0xff))))); }",
                    R(1).c_str(), W.c_str());
            break;
        case ARM_INS_UBFX: case ARM_INS_SBFX: {
            const unsigned lsb = unsigned(op(in, 2)->imm), w = unsigned(op(in, 3)->imm);
            if (in.id == ARM_INS_UBFX)
                c = fmt("%s = (%s >> %u) & 0x%xu;", W.c_str(), R(1).c_str(), lsb, w >= 32 ? 0xffffffffu : (1u << w) - 1);
            else
                c = fmt("%s = uint32_t(int32_t(%s << %u) >> %u);", W.c_str(), R(1).c_str(), 32 - lsb - w, 32 - w);
            break;
        }
        case ARM_INS_BFI: {
            const unsigned lsb = unsigned(op(in, 2)->imm), w = unsigned(op(in, 3)->imm);
            const uint32_t mask = (w >= 32 ? 0xffffffffu : ((1u << w) - 1)) << lsb;
            c = fmt("%s = (%s & 0x%xu) | ((%s << %u) & 0x%xu);", W.c_str(), W.c_str(), ~mask, R(1).c_str(), lsb, mask);
            break;
        }
        case ARM_INS_BFC: {
            const unsigned lsb = unsigned(op(in, 1)->imm), w = unsigned(op(in, 2)->imm);
            const uint32_t mask = (w >= 32 ? 0xffffffffu : ((1u << w) - 1)) << lsb;
            c = fmt("%s &= 0x%xu;", W.c_str(), ~mask);
            break;
        }
        case ARM_INS_UXTB: c = fmt("%s = %s & 0xffu;", W.c_str(), rot(1).c_str()); break;
        case ARM_INS_UXTH: c = fmt("%s = %s & 0xffffu;", W.c_str(), rot(1).c_str()); break;
        case ARM_INS_SXTB: c = fmt("%s = uint32_t(int32_t(int8_t(%s)));", W.c_str(), rot(1).c_str()); break;
        case ARM_INS_SXTH: c = fmt("%s = uint32_t(int32_t(int16_t(%s)));", W.c_str(), rot(1).c_str()); break;
        case ARM_INS_UXTB16: c = fmt("%s = %s & 0x00ff00ffu;", W.c_str(), rot(1).c_str()); break;
        case ARM_INS_SXTB16:
            c = fmt("{ const uint32_t v = %s; %s = (uint32_t(int32_t(int8_t(v))) & 0xffffu) | (uint32_t(int32_t(int8_t(v >> 16))) << 16); }",
                    rot(1).c_str(), W.c_str());
            break;
        case ARM_INS_UXTAB: c = fmt("%s = %s + (%s & 0xffu);", W.c_str(), R(1).c_str(), rot(2).c_str()); break;
        case ARM_INS_UXTAH: c = fmt("%s = %s + (%s & 0xffffu);", W.c_str(), R(1).c_str(), rot(2).c_str()); break;
        case ARM_INS_SXTAB: c = fmt("%s = %s + uint32_t(int32_t(int8_t(%s)));", W.c_str(), R(1).c_str(), rot(2).c_str()); break;
        case ARM_INS_SXTAH: c = fmt("%s = %s + uint32_t(int32_t(int16_t(%s)));", W.c_str(), R(1).c_str(), rot(2).c_str()); break;
        case ARM_INS_UXTAB16:
            c = fmt("{ const uint32_t a = %s, b = %s & 0x00ff00ffu; %s = ((a + b) & 0xffffu) | (((a >> 16) + (b >> 16)) << 16); }",
                    R(1).c_str(), rot(2).c_str(), W.c_str());
            break;
        case ARM_INS_PKHBT: case ARM_INS_PKHTB: {
            std::string pre, carry;
            const std::string m = shifted(in, *op(in, 2), false, pre, carry, tmp);
            if (in.id == ARM_INS_PKHBT)
                c = pre + fmt("%s = (%s & 0xffffu) | (%s & 0xffff0000u);", W.c_str(), R(1).c_str(), m.c_str());
            else
                c = pre + fmt("%s = (%s & 0xffff0000u) | (%s & 0xffffu);", W.c_str(), R(1).c_str(), m.c_str());
            break;
        }
        case ARM_INS_USAT: case ARM_INS_SSAT: {
            std::string pre, carry;
            const std::string m = shifted(in, *op(in, 2), false, pre, carry, tmp);
            const unsigned bits = unsigned(op(in, 1)->imm);
            c = pre + fmt("%s = recomp::%s(ctx, int32_t(%s), %u);", W.c_str(), in.id == ARM_INS_USAT ? "usat32" : "ssat32",
                          m.c_str(), bits);
            break;
        }
        case ARM_INS_QADD: c = fmt("%s = recomp::qadd32(ctx, %s, %s);", W.c_str(), R(1).c_str(), R(2).c_str()); break;
        case ARM_INS_QSUB: c = fmt("%s = recomp::qsub32(ctx, %s, %s);", W.c_str(), R(1).c_str(), R(2).c_str()); break;
        case ARM_INS_QDADD:
            c = fmt("%s = recomp::qadd32(ctx, %s, recomp::qadd32(ctx, %s, %s));", W.c_str(), R(1).c_str(), R(2).c_str(), R(2).c_str());
            break;
        case ARM_INS_QDSUB:
            c = fmt("%s = recomp::qsub32(ctx, %s, recomp::qadd32(ctx, %s, %s));", W.c_str(), R(1).c_str(), R(2).c_str(), R(2).c_str());
            break;
        case ARM_INS_MOVT:
            c = fmt("%s = (%s & 0xffffu) | (0x%xu << 16);", W.c_str(), W.c_str(), uint32_t(op(in, 1)->imm) & 0xffff);
            break;
        default:
            return false;
    }
    out.code += c + " ";
    return true;
}

// ------------------------------------------------------------------ single load / store
bool Lifter::lift_load_store(const Insn& in, LiftResult& out, int& tmp) {
    unsigned size = 4;
    bool load = true, sign = false, dual = false;
    switch (in.id) {
        case ARM_INS_LDR: case ARM_INS_LDRT: break;
        case ARM_INS_LDRB: case ARM_INS_LDRBT: size = 1; break;
        case ARM_INS_LDRH: case ARM_INS_LDRHT: size = 2; break;
        case ARM_INS_LDRSB: case ARM_INS_LDRSBT: size = 1; sign = true; break;
        case ARM_INS_LDRSH: case ARM_INS_LDRSHT: size = 2; sign = true; break;
        case ARM_INS_LDRD: dual = true; break;
        case ARM_INS_STR: case ARM_INS_STRT: load = false; break;
        case ARM_INS_STRB: case ARM_INS_STRBT: load = false; size = 1; break;
        case ARM_INS_STRH: case ARM_INS_STRHT: load = false; size = 2; break;
        case ARM_INS_STRD: load = false; dual = true; break;
        default: return false;
    }
    const unsigned mi = dual ? 2 : 1;
    const cs_arm_op* rt = op(in, 0);
    const cs_arm_op* rt2 = dual ? op(in, 1) : nullptr;
    const cs_arm_op* mem = op(in, mi);
    const cs_arm_op* post = op(in, mi + 1);  // post-indexed offset, if any
    if (!is_reg(rt) || (dual && !is_reg(rt2)) || !is_mem(mem)) return false;
    const bool has_post = post != nullptr;
    const bool writeback = has_post || in.arm.writeback;
    const int base = gpr(mem->mem.base);
    if (base < 0) return false;

    // Offset expression.
    std::string off;
    if (has_post) {
        if (is_imm(post)) {
            const uint32_t v = uint32_t(post->imm);
            off = post->subtracted ? fmt("(0u - %s)", hex(v).c_str()) : hex(v);
        } else if (is_reg(post)) {
            std::string pre, carry;
            off = shifted(in, *post, false, pre, carry, tmp);
            if (!pre.empty()) return false;
            if (post->subtracted) off = "(0u - " + off + ")";
        } else {
            return false;
        }
    } else if (mem->mem.index != ARM_REG_INVALID) {
        cs_arm_op io{};
        io.type = ARM_OP_REG;
        io.reg = mem->mem.index;
        io.shift = mem->shift;
        std::string pre, carry;
        off = shifted(in, io, false, pre, carry, tmp);
        if (!pre.empty()) return false;
        if (mem->subtracted) off = "(0u - " + off + ")";
    } else {
        off = hex(uint32_t(mem->mem.disp));
    }

    const char* ldfn = size == 4 ? "recomp::ld32" : size == 2 ? (sign ? "recomp::lds16" : "recomp::ld16")
                                                              : (sign ? "recomp::lds8" : "recomp::ld8");
    const char* stfn = size == 4 ? "recomp::st32" : size == 2 ? "recomp::st16" : "recomp::st8";

    // Literal pool load: fold to a constant.
    if (load && base == 15 && !writeback && mem->mem.index == ARM_REG_INVALID) {
        const uint32_t ea = (pc_of(in) & ~3u) + uint32_t(mem->mem.disp);
        uint64_t v;
        if (literal(ea, dual ? 8 : size, v)) {
            uint32_t lo = uint32_t(v);
            if (sign) lo = size == 1 ? uint32_t(int32_t(int8_t(lo))) : uint32_t(int32_t(int16_t(lo)));
            if (dual) {
                out.code += fmt("%s = %s; %s = %s; ", reg_lv(rt->reg).c_str(), hex(lo).c_str(), reg_lv(rt2->reg).c_str(),
                                hex(uint32_t(v >> 32)).c_str());
            } else if (gpr(rt->reg) == 15) {
                out.code += fmt("const uint32_t tgt = %s; ", hex(lo).c_str());
                out.new_pc = "tgt";
            } else {
                out.code += fmt("%s = %s; ", reg_lv(rt->reg).c_str(), hex(lo).c_str());
            }
            return true;
        }
    }

    const std::string b = read(in, mem->mem.base, true);
    std::string c = "{ ";
    c += has_post ? fmt("const uint32_t ea = %s; ", b.c_str()) : fmt("const uint32_t ea = %s + %s; ", b.c_str(), off.c_str());
    if (load) {
        if (dual) c += "const uint32_t v0 = recomp::ld32(ea), v1 = recomp::ld32(ea + 4); ";
        else c += fmt("const uint32_t v0 = %s(ea); ", ldfn);
        if (writeback) c += fmt("%s = %s; ", reg_lv(mem->mem.base).c_str(), has_post ? ("ea + " + off).c_str() : "ea");
        if (gpr(rt->reg) == 15) {
            out.code += c + "tgt = v0; } ";
            out.code = "uint32_t tgt; " + out.code;
            out.new_pc = "tgt";
            return true;
        }
        c += reg_lv(rt->reg) + " = v0; ";
        if (dual) c += reg_lv(rt2->reg) + " = v1; ";
    } else {
        if (dual) {
            c += fmt("recomp::st32(ea, %s); recomp::st32(ea + 4, %s); ", read(in, rt->reg).c_str(), read(in, rt2->reg).c_str());
        } else {
            c += fmt("%s(ea, %s); ", stfn, read(in, rt->reg).c_str());
        }
        if (writeback) c += fmt("%s = %s; ", reg_lv(mem->mem.base).c_str(), has_post ? ("ea + " + off).c_str() : "ea");
    }
    out.code += c + "} ";
    return true;
}

// ------------------------------------------------------------------ load / store multiple
bool Lifter::lift_multiple(const Insn& in, LiftResult& out) {
    bool load, wb = in.arm.writeback;
    unsigned first = 1;
    int base;
    enum { IA, IB, DA, DB } mode;
    switch (in.id) {
        case ARM_INS_PUSH: load = false; mode = DB; wb = true; first = 0; base = 13; break;
        case ARM_INS_POP: load = true; mode = IA; wb = true; first = 0; base = 13; break;
        case ARM_INS_LDM: load = true; mode = IA; break;
        case ARM_INS_LDMIB: load = true; mode = IB; break;
        case ARM_INS_LDMDA: load = true; mode = DA; break;
        case ARM_INS_LDMDB: load = true; mode = DB; break;
        case ARM_INS_STM: load = false; mode = IA; break;
        case ARM_INS_STMIB: load = false; mode = IB; break;
        case ARM_INS_STMDA: load = false; mode = DA; break;
        case ARM_INS_STMDB: load = false; mode = DB; break;
        default: return false;
    }
    if (in.arm.usermode) return false;  // "^" forms: system level
    if (first == 1) {
        if (!is_reg(op(in, 0))) return false;
        base = gpr(op(in, 0)->reg);
    }
    std::vector<int> regs;
    for (unsigned i = first; i < in.arm.op_count; ++i) {
        if (!is_reg(op(in, i))) return false;
        int r = gpr(op(in, i)->reg);
        if (r < 0) return false;
        regs.push_back(r);
    }
    const unsigned n = unsigned(regs.size());
    std::string c = fmt("{ const uint32_t b = ctx->r[%d]; ", base);
    const uint32_t start_off = mode == IA ? 0 : mode == IB ? 4 : mode == DA ? uint32_t(-int(4 * n) + 4) : uint32_t(-int(4 * n));
    const uint32_t wb_off = (mode == IA || mode == IB) ? 4 * n : uint32_t(-int(4 * n));
    bool pc_loaded = false;
    if (load) {
        for (unsigned i = 0; i < n; ++i)
            c += fmt("const uint32_t v%u = recomp::ld32(b + 0x%xu); ", i, start_off + 4 * i);
        if (wb) c += fmt("ctx->r[%d] = b + 0x%xu; ", base, wb_off);
        for (unsigned i = 0; i < n; ++i) {
            if (regs[i] == 15) {
                c += fmt("tgt = v%u; ", i);
                pc_loaded = true;
            } else {
                c += fmt("ctx->r[%d] = v%u; ", regs[i], i);
            }
        }
    } else {
        for (unsigned i = 0; i < n; ++i)
            c += fmt("recomp::st32(b + 0x%xu, %s); ", start_off + 4 * i,
                     regs[i] == 15 ? hex(pc_of(in)).c_str() : (regs[i] == base ? "b" : fmt("ctx->r[%d]", regs[i]).c_str()));
        if (wb) c += fmt("ctx->r[%d] = b + 0x%xu; ", base, wb_off);
    }
    c += "} ";
    if (pc_loaded) {
        out.code += "uint32_t tgt; " + c;
        out.new_pc = "tgt";
    } else {
        out.code += c;
    }
    return true;
}

// ------------------------------------------------------------------ exclusives
bool Lifter::lift_exclusive(const Insn& in, LiftResult& out) {
    auto addr = [&](const cs_arm_op* m) {
        return m->mem.disp ? fmt("%s + %s", read(in, m->mem.base).c_str(), hex(uint32_t(m->mem.disp)).c_str())
                           : read(in, m->mem.base);
    };
    switch (in.id) {
        case ARM_INS_LDREX: case ARM_INS_LDREXB: case ARM_INS_LDREXH: {
            const char* f = in.id == ARM_INS_LDREX ? "ldrex32" : in.id == ARM_INS_LDREXB ? "ldrex8" : "ldrex16";
            out.code += fmt("%s = recomp::%s(ctx, %s); ", reg_lv(op(in, 0)->reg).c_str(), f, addr(op(in, 1)).c_str());
            return true;
        }
        case ARM_INS_LDREXD:
            out.code += fmt("{ const uint64_t v = recomp::ldrexd(ctx, %s); %s = uint32_t(v); %s = uint32_t(v >> 32); } ",
                            addr(op(in, 2)).c_str(), reg_lv(op(in, 0)->reg).c_str(), reg_lv(op(in, 1)->reg).c_str());
            return true;
        case ARM_INS_STREX: case ARM_INS_STREXB: case ARM_INS_STREXH: {
            const char* f = in.id == ARM_INS_STREX ? "strex32" : in.id == ARM_INS_STREXB ? "strex8" : "strex16";
            out.code += fmt("%s = recomp::%s(ctx, %s, %s); ", reg_lv(op(in, 0)->reg).c_str(), f, addr(op(in, 2)).c_str(),
                            read(in, op(in, 1)->reg).c_str());
            return true;
        }
        case ARM_INS_STREXD:
            out.code += fmt("%s = recomp::strexd(ctx, %s, (uint64_t(%s) << 32) | %s); ", reg_lv(op(in, 0)->reg).c_str(),
                            addr(op(in, 3)).c_str(), read(in, op(in, 2)->reg).c_str(), read(in, op(in, 1)->reg).c_str());
            return true;
        case ARM_INS_CLREX:
            out.code += "recomp::clrex(ctx); ";
            return true;
        default:
            return false;
    }
}

// ------------------------------------------------------------------ misc / system / branches
bool Lifter::lift_misc(const Insn& in, LiftResult& out) {
    switch (in.id) {
        case ARM_INS_NOP: case ARM_INS_YIELD: case ARM_INS_WFE: case ARM_INS_WFI: case ARM_INS_SEV:
        case ARM_INS_HINT: case ARM_INS_PLD: case ARM_INS_PLDW: case ARM_INS_PLI: case ARM_INS_IT:
        case ARM_INS_B: case ARM_INS_BL: case ARM_INS_CBZ: case ARM_INS_CBNZ:
            return true;
        case ARM_INS_BLX:
            if (op(in, 0)->type == ARM_OP_IMM) return true;
            [[fallthrough]];
        case ARM_INS_BX:
            out.code += fmt("const uint32_t tgt = %s; ", read(in, op(in, 0)->reg).c_str());
            out.new_pc = "tgt";
            return true;
        case ARM_INS_TBB: case ARM_INS_TBH: {
            const cs_arm_op* m = op(in, 0);
            const std::string idx = read(in, m->mem.index);
            const std::string b = read(in, m->mem.base);
            if (in.id == ARM_INS_TBB)
                out.code += fmt("const uint32_t tgt = %s + 2 * recomp::ld8(%s + %s); ", b.c_str(), b.c_str(), idx.c_str());
            else
                out.code += fmt("const uint32_t tgt = %s + 2 * recomp::ld16(%s + (%s << 1)); ", b.c_str(), b.c_str(), idx.c_str());
            out.new_pc = "tgt";
            return true;
        }
        case ARM_INS_DMB: case ARM_INS_DSB: case ARM_INS_ISB:
            out.code += "recomp::barrier(); ";
            return true;
        case ARM_INS_SVC:
            out.code += fmt("recomp::svc(ctx, %s); ", hex(uint32_t(op(in, 0)->imm)).c_str());
            return true;
        case ARM_INS_MRC: {
            // mrc p15, 0, rt, c13, c0, 3  => TPIDRURO (thread pointer)
            const unsigned cp = unsigned(op(in, 0)->imm), op1 = unsigned(op(in, 1)->imm);
            const unsigned crn = unsigned(op(in, 3)->imm), crm = unsigned(op(in, 4)->imm), op2 = unsigned(op(in, 5)->imm);
            const int rt = gpr(op(in, 2)->reg);
            if (cp == 15 && op1 == 0 && crn == 13 && crm == 0 && op2 == 3 && rt >= 0 && rt < 15) {
                out.code += fmt("ctx->r[%d] = ctx->tls; ", rt);
                return true;
            }
            out.code += fmt("recomp::mrc(ctx, %s, %d, %s); ", hex(in.addr).c_str(), rt, hex(in.raw).c_str());
            return true;
        }
        case ARM_INS_MRS:
            out.code += fmt("%s = (uint32_t(ctx->n) << 31) | (uint32_t(ctx->z) << 30) | (uint32_t(ctx->c) << 29) | "
                            "(uint32_t(ctx->v) << 28) | (uint32_t(ctx->q) << 27) | (ctx->ge << 16) | 0x10u; ",
                            reg_lv(op(in, 0)->reg).c_str());
            return true;
        case ARM_INS_MSR: {
            // Only APSR fields are writable from user mode.
            const cs_arm_op* src = op(in, 1);
            const std::string v = is_reg(src) ? read(in, src->reg) : hex(uint32_t(src->imm));
            out.code += fmt("{ const uint32_t v = %s; ctx->n = v >> 31; ctx->z = (v >> 30) & 1; ctx->c = (v >> 29) & 1; "
                            "ctx->v = (v >> 28) & 1; ctx->q = (v >> 27) & 1; ctx->ge = (v >> 16) & 0xf; } ", v.c_str());
            return true;
        }
        case ARM_INS_UDF: case ARM_INS_BKPT:
            out.code += fmt("recomp::trap(ctx, %s, \"%s\"); ", hex(in.addr).c_str(), in.text().c_str());
            return true;
        case ARM_INS_ADR:
            out.code += fmt("%s = %s; ", reg_lv(op(in, 0)->reg).c_str(),
                            hex((pc_of(in) & ~3u) + uint32_t(op(in, 1)->imm)).c_str());
            return true;
        default:
            return false;
    }
}

LiftResult Lifter::lift(const Insn& in) {
    LiftResult out;
    int tmp = 0;
    const bool ok = lift_misc(in, out) || lift_data_processing(in, out, tmp) || lift_shift(in, out, tmp) ||
                    lift_load_store(in, out, tmp) || lift_multiple(in, out) || lift_multiply(in, out) ||
                    lift_bits(in, out, tmp) || lift_exclusive(in, out) || lift_vfp(in, out);
    if (!ok) {
        std::string text = in.text();
        for (auto& ch : text)
            if (ch == '"' || ch == '\\') ch = '\'';
        out = {};
        out.code = fmt("recomp::unimplemented(ctx, %s, \"%s\"); ", hex(in.addr).c_str(), text.c_str());
        out.unimplemented = true;
    }
    (void)is_cond;
    return out;
}

}  // namespace recomp
