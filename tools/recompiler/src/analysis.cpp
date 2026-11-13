// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace recomp {
namespace {

constexpr uint8_t STT_FUNC = 2;

uint32_t prel31(const ElfImage& img, uint32_t addr) {
    uint32_t v = img.read32(addr) & 0x7fffffff;
    if (v & 0x40000000) v |= 0x80000000;  // sign-extend 31 -> 32 bits
    return addr + v;
}

bool is_cond(const Insn& in) { return in.arm.cc != ARM_CC_AL && in.arm.cc != ARM_CC_INVALID; }

const cs_arm_op* op(const Insn& in, unsigned i) {
    return i < in.arm.op_count ? &in.arm.operands[i] : nullptr;
}

bool op_is_reg(const Insn& in, unsigned i, unsigned reg) {
    const cs_arm_op* o = op(in, i);
    return o && o->type == ARM_OP_REG && unsigned(o->reg) == reg;
}

bool reglist_has_pc(const Insn& in, unsigned first) {
    for (unsigned i = first; i < in.arm.op_count; ++i)
        if (op_is_reg(in, i, ARM_REG_PC)) return true;
    return false;
}

// PC value as seen by the instruction.
uint32_t pc_of(const Insn& in) { return in.addr + (in.mode == Mode::Thumb ? 4 : 8); }

}  // namespace

const char* flow_name(Flow f) {
    switch (f) {
        case Flow::Normal: return "normal";
        case Flow::Branch: return "branch";
        case Flow::TailCall: return "tailcall";
        case Flow::Call: return "call";
        case Flow::CallIndirect: return "call-indirect";
        case Flow::Return: return "return";
        case Flow::IndirectJump: return "indirect-jump";
        case Flow::JumpTable: return "jump-table";
        case Flow::Invalid: return "invalid";
    }
    return "?";
}

Analysis::Analysis(const ElfImage& image, Disassembler& dis) : image_(image), dis_(dis) {
    noreturn_names_ = {
        "abort", "exit", "_exit", "__stack_chk_fail", "longjmp", "siglongjmp", "__assert2",
        "pthread_exit", "__cxa_throw", "__cxa_rethrow", "_Unwind_Resume", "__cxa_bad_cast",
        "__cxa_bad_typeid", "__cxa_pure_virtual", "__cxa_deleted_virtual",
        "__cxa_throw_bad_array_length", "__cxa_throw_bad_array_new_length",
        "__cxa_call_unexpected", "_ZSt9terminatev", "_ZSt10unexpectedv",
        "_ZN10__cxxabiv111__terminateEPFvvE", "_ZN10__cxxabiv112__unexpectedEPFvvE",
        "restore_core_regs", "__restore_core_regs",  // the unwinder's final jump (a runtime hook)
    };
}

std::string Analysis::name_of(uint32_t addr) const {
    if (auto it = image_.plt_imports().find(addr); it != image_.plt_imports().end()) return it->second;
    if (auto it = names_.find(addr); it != names_.end()) return it->second;
    return {};
}

void Analysis::parse_exidx() {
    exidx_starts_sorted_.clear();
    const Section* s = image_.section(".ARM.exidx");
    if (!s) return;
    for (uint32_t a = s->addr; a + 8 <= s->addr + s->size; a += 8) {
        ExidxEntry e;
        e.start = prel31(image_, a);
        e.data = image_.read32(a + 4);
        e.cant_unwind = e.data == 1;
        exidx_.push_back(e);
    }
    std::sort(exidx_.begin(), exidx_.end(), [](auto& x, auto& y) { return x.start < y.start; });
    for (const auto& e : exidx_) exidx_starts_sorted_.push_back(e.start);
}

// C++ landing pads from the GCC LSDAs in .ARM.extab (generic personality
// model: prel31 personality, unwind opcodes, then the LSDA).
void Analysis::parse_lsda() {
    auto uleb = [&](uint32_t& p) {
        uint32_t v = 0, shift = 0;
        uint8_t b;
        do {
            b = image_.read8(p++);
            if (shift < 32) v |= uint32_t(b & 0x7f) << shift;
            shift += 7;
        } while (b & 0x80);
        return v;
    };
    auto encoded = [&](uint32_t& p, uint8_t enc, bool& ok) -> uint32_t {
        const uint32_t at = p;
        uint32_t v = 0;
        switch (enc & 0x0f) {
            case 0x00: case 0x03: case 0x0b: v = image_.read32(p); p += 4; break;  // absptr / (s|u)data4
            case 0x01: v = uleb(p); break;
            case 0x02: case 0x0a: v = image_.read16(p); p += 2; break;
            case 0x09: {  // sleb128
                uint32_t shift = 0;
                uint8_t b;
                do {
                    b = image_.read8(p++);
                    if (shift < 32) v |= uint32_t(b & 0x7f) << shift;
                    shift += 7;
                } while (b & 0x80);
                if (shift < 32 && (b & 0x40)) v |= ~0u << shift;
                break;
            }
            default: ok = false; return 0;
        }
        if ((enc & 0x70) == 0x10) v += at;  // pcrel
        else if ((enc & 0x70) != 0) ok = false;
        return v;
    };
    const Section* exidx = image_.section(".ARM.exidx");
    if (!exidx) return;
    size_t pads = 0;
    for (uint32_t a = exidx->addr; a + 8 <= exidx->addr + exidx->size; a += 8) {
        const uint32_t start = prel31(image_, a), data = image_.read32(a + 4);
        if (data == 1 || (data & 0x80000000)) continue;  // cantunwind / inline compact entry
        const uint32_t tab = prel31(image_, a + 4);
        if (!image_.contains(tab, 8) || (image_.read32(tab) & 0x80000000)) continue;  // compact model in extab
        const uint32_t extra_words = image_.read8(tab + 7);  // top byte of the first opcode word
        uint32_t p = tab + 4 + 4 * (1 + extra_words);
        if (!image_.contains(p, 4)) continue;
        bool ok = true;
        uint32_t lp_start = start;
        const uint8_t lp_enc = image_.read8(p++);
        if (lp_enc != 0xff) lp_start = encoded(p, lp_enc, ok);
        const uint8_t tt_enc = image_.read8(p++);
        if (tt_enc != 0xff) uleb(p);
        const uint8_t cs_enc = image_.read8(p++);
        const uint32_t cs_len = uleb(p);
        const uint32_t cs_end = p + cs_len;
        if (!ok || cs_len > 0x10000 || !image_.contains(p, cs_len)) continue;
        std::set<uint32_t> found;
        while (ok && p < cs_end) {
            encoded(p, cs_enc, ok);  // call-site start
            encoded(p, cs_enc, ok);  // length
            const uint32_t lp = encoded(p, cs_enc, ok);
            uleb(p);  // action
            if (ok && lp) found.insert((lp_start + lp) & ~1u);
        }
        if (!ok || found.empty()) continue;
        region_pads_[start].insert(found.begin(), found.end());
        pads += found.size();
    }
    if (std::getenv("ABRECOMP_DEBUG_EH")) std::fprintf(stderr, "lsda: %zu landing pads in %zu regions\n", pads, region_pads_.size());
}

uint32_t Analysis::exidx_range_end(uint32_t addr, uint32_t* begin) const {
    auto it = std::upper_bound(exidx_.begin(), exidx_.end(), addr,
                               [](uint32_t a, const ExidxEntry& e) { return a < e.start; });
    if (begin) *begin = it == exidx_.begin() ? image_.text_begin() : std::prev(it)->start;
    return it == exidx_.end() ? image_.text_end() : it->start;
}

Mode Analysis::guess_mode(uint32_t addr) const {
    // Exidx gives no mode. Decode linearly in both modes and score how
    // plausible the result is: calls that land exactly on known function
    // starts or PLT stubs are strong evidence; invalid encodings, branches
    // leaving the region and stray unconditional-space ARM words are not.
    if (addr & 2) return Mode::Thumb;
    double arm = mode_score(addr, Mode::Arm), thumb = mode_score(addr, Mode::Thumb);
    if (std::getenv("ABRECOMP_DEBUG_MODE")) std::fprintf(stderr, "guess %x: arm %.1f thumb %.1f\n", addr, arm, thumb);
    const uint32_t w = image_.read32(addr);
    const uint16_t h = image_.read16(addr);
    if ((w & 0xffff0000) == 0xe92d0000) arm += 6;                                       // push {..}
    if ((h & 0xfe00) == 0xb400 || h == 0xe92d || (h & 0xff80) == 0xb080) thumb += 6;  // push / push.w / sub sp
    return thumb > arm ? Mode::Thumb : Mode::Arm;
}

double Analysis::mode_score(uint32_t addr, Mode m, bool path_only) const {
    uint32_t region_begin = 0;
    const uint32_t region_end = exidx_range_end(addr, &region_begin);
    auto known_entry = [&](uint32_t t) {
        return image_.plt_imports().count(t) || std::binary_search(exidx_starts_sorted_.begin(), exidx_starts_sorted_.end(), t);
    };
    double sc = 0;
    uint32_t a = addr;
    for (int i = 0; i < 48 && a < region_end; ++i) {
        const Insn* in = const_cast<Disassembler&>(dis_).decode(a, m);
        if (!in) return sc - 10;
        if (m == Mode::Arm && (in->raw >> 28) == 0xf && in->id != ARM_INS_BLX && in->id != ARM_INS_PLD &&
            in->id != ARM_INS_DMB && in->id != ARM_INS_DSB && in->id != ARM_INS_ISB && !in->in_group(ARM_GRP_NEON))
            sc -= 3;
        switch (in->id) {
            case ARM_INS_BL:
            case ARM_INS_BLX:
                if (in->arm.operands[0].type == ARM_OP_IMM) {
                    uint32_t t = uint32_t(in->arm.operands[0].imm);
                    if (in->id == ARM_INS_BLX && m == Mode::Thumb) t &= ~3u;
                    if (known_entry(t)) sc += 4;
                    else if (t < image_.text_begin() || t >= image_.text_end()) sc -= 4;
                }
                break;
            case ARM_INS_B: {
                uint32_t t = uint32_t(in->arm.operands[0].imm);
                if (t < region_begin || t >= region_end) sc += known_entry(t) ? 2 : -4;
                break;
            }
            default:
                break;
        }
        InsnInfo info = const_cast<Analysis*>(this)->classify(Function{}, *in);
        if (info.flow == Flow::Return) {
            sc += 2;
            if (!info.conditional) break;
        }
        if (info.flow == Flow::Invalid) return sc - 10;
        // What follows an unconditional branch is not on this path.
        if (path_only && (info.flow == Flow::Branch || info.flow == Flow::TailCall) && !info.conditional) break;
        a += in->size;
    }
    return sc;
}

void Analysis::add_entry(uint32_t addr, Mode mode, const char* why) {
    if (!image_.is_exec(addr) || addr < image_.text_begin() || addr >= image_.text_end()) return;
    const std::string w = why;
    if (w != "call" && w != "tailcall") roots_.insert(addr);
    if (functions_.count(addr) || pending_.count(addr)) return;
    pending_[addr] = {mode, why};
}

void Analysis::seed_entries() {
    for (const auto& s : image_.dynsyms()) {
        if (!s.defined() || s.type != STT_FUNC) continue;
        const uint32_t a = s.value & ~1u;
        names_.emplace(a, s.name);
        add_entry(a, (s.value & 1) ? Mode::Thumb : Mode::Arm, "dynsym");
        if (noreturn_names_.count(s.name)) noreturn_.insert(a);
    }
    // The unwinder's final jump when its symbol is hidden: libgcc's
    // restore_core_regs or LLVM libunwind's Registers_arm::restoreCoreAndJumpTo
    // (both load r0-r15 from the block at r0).
    static const std::vector<std::vector<uint32_t>> kRestoreCoreRegs = {
        {0xe2801034, 0xe8910038, 0xe92d0038, 0xe8900fff, 0xe89de000},
        {0xe1a0e000, 0xe89e1fff, 0xe59ed034, 0xe59ee03c, 0xe12fff1e},
    };
    for (uint32_t a = image_.text_begin(); a + 20 <= image_.text_end(); a += 4)
        for (const auto& pat : kRestoreCoreRegs) {
            bool match = true;
            for (size_t i = 0; i < pat.size() && match; ++i) match = image_.read32(a + 4 * uint32_t(i)) == pat[i];
            if (!match) continue;
            names_.emplace(a, "restore_core_regs");
            noreturn_.insert(a);
            add_entry(a, Mode::Arm, "unwinder");
        }
    for (const char* sec : {".init_array", ".fini_array"}) {
        if (const Section* s = image_.section(sec))
            for (uint32_t a = s->addr; a + 4 <= s->addr + s->size; a += 4) {
                uint32_t v = image_.read32(a);
                if (v != 0 && v != 0xffffffff) add_entry(v & ~1u, (v & 1) ? Mode::Thumb : Mode::Arm, sec);
            }
    }
    // Code pointers in data (vtables, callback tables): bit 0 is the mode.
    // Odd values are unambiguous Thumb pointers; even values are only taken
    // when they hit an exidx function start (otherwise they may be labels,
    // e.g. ARM jump-table entries).
    std::unordered_set<uint32_t> exidx_starts;
    for (const auto& e : exidx_) exidx_starts.insert(e.start);
    for (const auto& r : image_.relocations()) {
        if ((r.type != R_ARM_RELATIVE && r.type != R_ARM_ABS32) || !image_.contains(r.offset, 4)) continue;
        uint32_t v = image_.read32(r.offset);
        if (r.type == R_ARM_ABS32) {
            if (r.sym >= image_.dynsyms().size()) continue;
            v += image_.dynsyms()[r.sym].value;
        }
        if (v < image_.text_begin() || v >= image_.text_end()) continue;
        if (v & 1) add_entry(v & ~1u, Mode::Thumb, "reloc");
        else if (exidx_starts.count(v)) add_entry(v, Mode::Arm, "reloc");
        else even_code_refs_.push_back(v);
    }
}

InsnInfo Analysis::classify(const Function& fn, const Insn& in) {
    InsnInfo info;
    info.insn = &in;
    info.conditional = is_cond(in);
    auto direct = [&](uint32_t target, Mode m, bool is_call) {
        info.target = target;
        info.target_mode = m;
        if (is_call) {
            info.flow = Flow::Call;
        } else {
            const bool outside = target < fn.range_begin || target >= fn.range_end;
            // B never switches mode, so only a same-mode entry can be a tail-call target.
            auto f = functions_.find(target);
            const bool other_entry = target != fn.entry && f != functions_.end() && f->second.mode == in.mode;
            info.flow = (outside || other_entry || target == fn.entry) ? Flow::TailCall : Flow::Branch;
        }
    };
    const Mode other = in.mode == Mode::Arm ? Mode::Thumb : Mode::Arm;

    switch (in.id) {
        case ARM_INS_B:
            direct(uint32_t(in.arm.operands[0].imm), in.mode, false);
            return info;
        case ARM_INS_CBZ:
        case ARM_INS_CBNZ:
            direct(uint32_t(in.arm.operands[1].imm), in.mode, false);
            info.conditional = true;
            return info;
        case ARM_INS_BL:
            direct(uint32_t(in.arm.operands[0].imm), in.mode, true);
            return info;
        case ARM_INS_BLX:
            if (op(in, 0)->type == ARM_OP_IMM) {
                uint32_t t = uint32_t(in.arm.operands[0].imm);
                direct(other == Mode::Arm ? t & ~3u : t, other, true);
            } else {
                info.flow = Flow::CallIndirect;
            }
            return info;
        case ARM_INS_BX:
            if (op_is_reg(in, 0, ARM_REG_PC)) {
                // Interworking veneer: Thumb "bx pc" continues in ARM mode at Align(pc, 4).
                direct(pc_of(in) & ~3u, in.mode == Mode::Thumb ? Mode::Arm : in.mode, false);
                info.flow = Flow::TailCall;
                return info;
            }
            // "bx rN" may be a switch ("adr; ldr; add; bx"); it falls back to IndirectJump.
            info.flow = op_is_reg(in, 0, ARM_REG_LR) ? Flow::Return : Flow::JumpTable;
            return info;
        case ARM_INS_TBB:
        case ARM_INS_TBH:
            info.flow = Flow::JumpTable;
            return info;
        case ARM_INS_POP:
            if (reglist_has_pc(in, 0)) info.flow = Flow::Return;
            return info;
        case ARM_INS_LDM:
        case ARM_INS_LDMIB:
        case ARM_INS_LDMDA:
        case ARM_INS_LDMDB:
            if (reglist_has_pc(in, 1))
                info.flow = op_is_reg(in, 0, ARM_REG_SP) ? Flow::Return : Flow::IndirectJump;
            return info;
        case ARM_INS_SVC:
            return info;
        case ARM_INS_UDF:
        case ARM_INS_BKPT:
            info.flow = Flow::Invalid;
            return info;
        default:
            break;
    }
    // Any other instruction with PC as destination. Capstone does not always
    // list PC in regs_write, so decide by operand role instead.
    auto op0_is_dest = [](unsigned id) {
        switch (id) {
            case ARM_INS_STR: case ARM_INS_STRB: case ARM_INS_STRH: case ARM_INS_STRD:
            case ARM_INS_STM: case ARM_INS_STMDB: case ARM_INS_STMIB: case ARM_INS_STMDA:
            case ARM_INS_PUSH: case ARM_INS_CMP: case ARM_INS_CMN: case ARM_INS_TST:
            case ARM_INS_TEQ: case ARM_INS_PLD: case ARM_INS_PLI: case ARM_INS_VMOV:
                return false;
            default:
                return true;
        }
    };
    if (op_is_reg(in, 0, ARM_REG_PC) && op0_is_dest(in.id)) {
        if ((in.id == ARM_INS_MOV) && op_is_reg(in, 1, ARM_REG_LR)) {
            info.flow = Flow::Return;
        } else if (in.id == ARM_INS_LDR && op(in, 1)->type == ARM_OP_MEM &&
                   op(in, 1)->mem.base == ARM_REG_SP && in.arm.writeback) {
            info.flow = Flow::Return;  // ldr pc, [sp], #4
        } else if (in.id == ARM_INS_ADD && in.mode == Mode::Arm && op_is_reg(in, 1, ARM_REG_IP) &&
                   op_is_reg(in, 2, ARM_REG_PC) && !is_cond(in)) {
            // Long-branch veneer: ldr ip, [pc, #d] ; add pc, ip, pc
            const Insn* ld = dis_.decode(in.addr - 4, in.mode);
            if (ld && ld->id == ARM_INS_LDR && op_is_reg(*ld, 0, ARM_REG_IP) && op(*ld, 1)->type == ARM_OP_MEM &&
                op(*ld, 1)->mem.base == ARM_REG_PC && op(*ld, 1)->mem.index == ARM_REG_INVALID) {
                uint32_t t = image_.read32(pc_of(*ld) + uint32_t(op(*ld, 1)->mem.disp)) + pc_of(in);
                info.flow = Flow::TailCall;
                info.target = t & ~1u;
                info.target_mode = (t & 1) ? Mode::Thumb : Mode::Arm;
                return info;
            }
            info.flow = Flow::IndirectJump;
        } else if ((in.id == ARM_INS_LDR || in.id == ARM_INS_ADD) &&
                   ((in.id == ARM_INS_LDR && op(in, 1)->type == ARM_OP_MEM && op(in, 1)->mem.base == ARM_REG_PC) ||
                    (in.id == ARM_INS_ADD && op_is_reg(in, 1, ARM_REG_PC)))) {
            info.flow = Flow::JumpTable;
        } else if (in.id == ARM_INS_ADD || in.id == ARM_INS_MOV || in.id == ARM_INS_LDR) {
            info.flow = Flow::JumpTable;  // maybe a computed switch; falls back to IndirectJump
        } else {
            info.flow = Flow::IndirectJump;
        }
    }
    (void)other;
    return info;
}

bool Analysis::find_bound(const Function& fn, const Insn& dispatch, uint32_t before, unsigned reg,
                          uint32_t& count) const {
    // Walk back over already-discovered instructions looking for "cmp reg, #imm"
    // and the conditional branch / condition that guards the table.
    auto it = fn.insns.lower_bound(before);
    int guard_cc = ARM_CC_INVALID;
    if (is_cond(dispatch)) guard_cc = dispatch.arm.cc;  // e.g. ldrls pc, [...]: runs when cc holds
    for (int steps = 0; steps < 24 && it != fn.insns.begin(); ++steps) {
        --it;
        const Insn* in = it->second.insn;
        if (!in) return false;
        if (in->id == ARM_INS_B && is_cond(*in) && guard_cc == ARM_CC_INVALID) {
            // Branch to default case; table executes when the opposite condition holds.
            switch (in->arm.cc) {
                case ARM_CC_HI: guard_cc = ARM_CC_LS; break;
                case ARM_CC_HS: guard_cc = ARM_CC_LO; break;
                default: return false;
            }
            continue;
        }
        if (in->id == ARM_INS_CMP && op_is_reg(*in, 0, reg) &&
            (op(*in, 1)->type == ARM_OP_IMM || op(*in, 1)->type == ARM_OP_REG)) {
            uint32_t imm = 0;
            if (op(*in, 1)->type == ARM_OP_IMM) {
                imm = uint32_t(op(*in, 1)->imm);
            } else if (!const_reg_before(fn, in->addr, op(*in, 1)->reg, imm)) {
                return false;
            }
            if (guard_cc == ARM_CC_LS) count = imm + 1;
            else if (guard_cc == ARM_CC_LO) count = imm;
            else return false;
            return count > 0 && count <= 4096;
        }
        if (in->writes_reg(reg)) return false;
        for (uint8_t i = 0; i < in->arm.op_count; ++i)
            if (in->arm.operands[i].type == ARM_OP_REG && unsigned(in->arm.operands[i].reg) == reg &&
                i == 0 && in->id != ARM_INS_CMP && in->id != ARM_INS_TST && in->id != ARM_INS_STR)
                return false;  // reg redefined
    }
    return false;
}

// Value of a register set by "mov reg, #imm" a few instructions before addr.
bool Analysis::const_reg_before(const Function& fn, uint32_t addr, unsigned reg, uint32_t& value) const {
    auto it = fn.insns.lower_bound(addr);
    for (int steps = 0; steps < 32 && it != fn.insns.begin(); ++steps) {
        --it;
        const Insn* in = it->second.insn;
        if (!in) return false;
        if ((in->id == ARM_INS_MOV || in->id == ARM_INS_MOVW) && op_is_reg(*in, 0, reg) && !is_cond(*in) &&
            op(*in, 1)->type == ARM_OP_IMM) {
            value = uint32_t(op(*in, 1)->imm);
            return true;
        }
        if (in->writes_reg(reg) || op_is_reg(*in, 0, reg)) {
            if (in->id != ARM_INS_CMP && in->id != ARM_INS_TST && in->id != ARM_INS_STR) return false;
        }
    }
    return false;
}

// Value of `reg` built from a literal-pool load or a movw/movt pair shortly
// before `addr` (the usual way PIC code forms pc-relative offsets).
bool Analysis::literal_reg_before(const Function& fn, uint32_t addr, unsigned reg, uint32_t& value) const {
    auto it = fn.insns.lower_bound(addr);
    bool have_high = false;
    uint32_t high = 0;
    for (int steps = 0; steps < 16 && it != fn.insns.begin(); ++steps) {
        --it;
        const Insn* in = it->second.insn;
        if (!in) return false;
        if (it->second.flow != Flow::Normal && it->second.flow != Flow::Call) return false;  // keep to straight-line code
        if (!(in->writes_reg(reg) || op_is_reg(*in, 0, reg))) continue;
        if (is_cond(*in)) return false;
        if (in->id == ARM_INS_STR || in->id == ARM_INS_CMP || in->id == ARM_INS_TST) continue;
        if (in->id == ARM_INS_MOVT && op(*in, 1) && op(*in, 1)->type == ARM_OP_IMM && !have_high) {
            have_high = true;
            high = uint32_t(op(*in, 1)->imm) & 0xffff;
            continue;
        }
        if (in->id == ARM_INS_MOVW && op(*in, 1) && op(*in, 1)->type == ARM_OP_IMM) {
            value = (uint32_t(op(*in, 1)->imm) & 0xffff) | (high << 16);
            return true;
        }
        if (in->id == ARM_INS_LDR && !have_high && in->arm.op_count == 2 && op(*in, 1)->type == ARM_OP_MEM &&
            op(*in, 1)->mem.base == ARM_REG_PC && op(*in, 1)->mem.index == ARM_REG_INVALID) {
            const uint32_t lit = (pc_of(*in) & ~3u) + uint32_t(op(*in, 1)->mem.disp);
            if (!image_.contains(lit, 4)) return false;
            value = image_.read32(lit);
            return true;
        }
        return false;
    }
    return false;
}

// Function addresses formed pc-relatively ("ldr rM, =off; add rD, pc, rM"),
// e.g. callbacks stored in std::function or handed to pthread_create. They
// need no relocation, so this is the only evidence for such entries.
void Analysis::scan_code_pointers(const Function& fn) {
    for (const auto& [a, info] : fn.insns) {
        const Insn* in = info.insn;
        if (!in || in->id != ARM_INS_ADD || is_cond(*in)) continue;
        unsigned other = ARM_REG_INVALID;
        if (in->arm.op_count == 2 && op_is_reg(*in, 1, ARM_REG_PC)) other = op(*in, 0)->reg;  // Thumb "add rd, pc"
        else if (in->arm.op_count == 3 && op_is_reg(*in, 1, ARM_REG_PC) && op(*in, 2)->type == ARM_OP_REG &&
                 op(*in, 2)->shift.type == ARM_SFT_INVALID)
            other = op(*in, 2)->reg;
        else if (in->arm.op_count == 3 && op_is_reg(*in, 2, ARM_REG_PC) && op(*in, 1)->type == ARM_OP_REG)
            other = op(*in, 1)->reg;
        static const char* dbg_env = std::getenv("ABRECOMP_DEBUG_PCREL");
        const bool dbg = dbg_env && std::strtoul(dbg_env, nullptr, 16) == a;
        if (dbg) std::fprintf(stderr, "pcrel %x: other=%u ops=%u\n", a, other, in->arm.op_count);
        if (other == ARM_REG_INVALID || other == ARM_REG_PC) continue;
        uint32_t off;
        if (!literal_reg_before(fn, a, other, off)) {
            if (dbg) std::fprintf(stderr, "pcrel %x: no literal\n", a);
            continue;
        }
        if (dbg) std::fprintf(stderr, "pcrel %x: target %x\n", a, pc_of(*in) + off);
        const uint32_t t = pc_of(*in) + off;
        if (t < image_.text_begin() || t >= image_.text_end() || is_import_stub(t & ~1u)) continue;
        if (t & 1) {
            add_entry(t & ~1u, Mode::Thumb, "pcrel");
            continue;
        }
        // Even: an ARM function, unless it points into a known body (a label
        // or a table) or does not look like ARM code.
        auto owner = insn_owner_.find(t);
        if (dbg && owner != insn_owner_.end()) std::fprintf(stderr, "pcrel %x: %x is inside %x\n", a, t, owner->second);
        if (owner != insn_owner_.end() && owner->second != t) {
            rejected_refs_.push_back(t);
            continue;
        }
        if (functions_.count(t) || guess_mode(t) == Mode::Arm) add_entry(t, Mode::Arm, "pcrel");
    }
}

// "cmp idx, #N ; bls <table>" where the table is the branch target.
bool Analysis::find_branch_guard(const Function& fn, uint32_t table_insn, unsigned reg, uint32_t& count) const {
    for (auto it = fn.insns.begin(); it != fn.insns.end(); ++it) {
        const InsnInfo& info = it->second;
        if (info.flow != Flow::Branch || info.target != table_insn || !info.conditional || it == fn.insns.begin())
            continue;
        const int cc = info.insn->arm.cc;
        if (cc != ARM_CC_LS && cc != ARM_CC_LO) continue;
        const Insn* c = std::prev(it)->second.insn;
        if (!c || c->id != ARM_INS_CMP || !op_is_reg(*c, 0, reg) || op(*c, 1)->type != ARM_OP_IMM) continue;
        count = uint32_t(op(*c, 1)->imm) + (cc == ARM_CC_LS ? 1 : 0);
        return count > 0 && count <= 4096;
    }
    return false;
}

bool Analysis::resolve_jump_table(Function& fn, const Insn& in, JumpTable& jt) {
    uint32_t count = 0;
    const cs_arm_op* o1 = op(in, 1);
    if (in.id == ARM_INS_TBB || in.id == ARM_INS_TBH) {
        const cs_arm_op* m = op(in, 0);
        if (!m || m->type != ARM_OP_MEM || m->mem.base != ARM_REG_PC) return false;
        jt.kind = in.id == ARM_INS_TBB ? JumpTable::Kind::Tbb : JumpTable::Kind::Tbh;
        jt.index_reg = m->mem.index;
        jt.table_addr = in.addr + 4;
        if (!find_bound(fn, in, in.addr, jt.index_reg, count) && !find_branch_guard(fn, in.addr, jt.index_reg, count))
            return false;
        const unsigned esz = jt.kind == JumpTable::Kind::Tbb ? 1 : 2;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t e = esz == 1 ? image_.read8(jt.table_addr + i) : image_.read16(jt.table_addr + 2 * i);
            jt.targets.push_back(jt.table_addr + 2 * e);
        }
        for (uint32_t b = jt.table_addr; b < jt.table_addr + count * esz; b += 2) fn.data_words.insert(b & ~3u);
        return true;
    }
    if (in.id == ARM_INS_LDR && o1 && o1->type == ARM_OP_MEM && o1->mem.base == ARM_REG_PC &&
        o1->mem.index != ARM_REG_INVALID && o1->shift.type == ARM_SFT_LSL && o1->shift.value == 2 &&
        in.mode == Mode::Arm) {
        jt.kind = JumpTable::Kind::LdrPc;
        jt.index_reg = o1->mem.index;
        jt.table_addr = pc_of(in);
        if (!find_bound(fn, in, in.addr, jt.index_reg, count)) return false;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t a = jt.table_addr + 4 * i;
            jt.targets.push_back(image_.read32(a) & ~1u);
            fn.data_words.insert(a);
        }
        return true;
    }
    if (in.id == ARM_INS_ADD && in.mode == Mode::Arm && op_is_reg(in, 1, ARM_REG_PC)) {
        const cs_arm_op* o2 = op(in, 2);
        if (!o2 || o2->type != ARM_OP_REG || o2->shift.type != ARM_SFT_LSL || o2->shift.value != 2) return false;
        jt.kind = JumpTable::Kind::AddPcBranches;
        jt.index_reg = o2->reg;
        jt.table_addr = pc_of(in);
        if (!find_bound(fn, in, in.addr, jt.index_reg, count)) return false;
        for (uint32_t i = 0; i < count; ++i) jt.targets.push_back(jt.table_addr + 4 * i);
        return true;  // table entries are real instructions (branches); analyzed as code
    }
    return resolve_computed_jump(fn, in, jt) || resolve_offset_table(fn, in, jt);
}

// Clang's ARM switch without a bound in reach (the guard is in another block):
//   add rT, pc, #k ; ... ; ldr rX, [rI, rT] ; add pc, rX, rT
// The table of offsets from its own start follows the code; it ends where the
// lowest case begins.
bool Analysis::resolve_offset_table(Function& fn, const Insn& in, JumpTable& jt) {
    if (in.id != ARM_INS_ADD || in.mode != Mode::Arm || is_cond(in) || in.arm.op_count != 3) return false;
    const cs_arm_op *a = op(in, 1), *b = op(in, 2);
    if (a->type != ARM_OP_REG || b->type != ARM_OP_REG || b->shift.type != ARM_SFT_INVALID) return false;
    uint32_t table = 0;
    bool loaded = false;
    unsigned treg = ARM_REG_INVALID;
    for (uint32_t p = in.addr - 4; p + 6 * 4 >= in.addr && p >= fn.range_begin; p -= 4) {
        auto it = fn.insns.find(p);
        if (it == fn.insns.end() || !it->second.insn) return false;
        const Insn& x = *it->second.insn;
        if (!loaded) {
            // ldr rX, [rI, rT] with rX/rT the two operands of the dispatch
            const cs_arm_op* m = op(x, 1);
            if (x.id != ARM_INS_LDR || is_cond(x) || !m || m->type != ARM_OP_MEM || m->mem.index == ARM_REG_INVALID ||
                x.arm.writeback || m->subtracted)
                continue;
            const int dst = op(x, 0)->reg;
            if (dst == a->reg && (m->mem.base == b->reg || m->mem.index == b->reg)) treg = b->reg;
            else if (dst == b->reg && (m->mem.base == a->reg || m->mem.index == a->reg)) treg = a->reg;
            else return false;
            loaded = true;
        } else if (x.writes_reg(treg) || op_is_reg(x, 0, treg)) {
            if (x.id != ARM_INS_ADD || !op_is_reg(x, 1, ARM_REG_PC) || op(x, 2)->type != ARM_OP_IMM) return false;
            table = pc_of(x) + uint32_t(op(x, 2)->imm);
            break;
        }
    }
    if (!table || table < in.addr + 4 || table >= fn.range_end) return false;
    uint32_t end = fn.range_end;
    for (uint32_t e = table; e + 4 <= end; e += 4) {
        const uint32_t t = table + image_.read32(e);
        if (t & 3 || t < e + 4 || t >= fn.range_end) break;
        jt.targets.push_back(t);
        fn.data_words.insert(e);
        end = std::min(end, t);
    }
    if (jt.targets.empty()) return false;
    jt.kind = JumpTable::Kind::Computed;
    jt.table_addr = table;
    return true;
}

namespace {

// Symbolic value for switch recovery: Unknown, c, idx*scale + c, or
// load<width>(base + idx*scale) + c.
struct Sym {
    enum Kind : uint8_t { Unknown, Const, Idx, Load } kind = Unknown;
    uint32_t c = 0;      // additive constant
    uint32_t scale = 0;  // Idx: multiplier; Load: element scale
    uint32_t base = 0;   // Load: table address
    uint8_t width = 0;   // Load: element size in bytes
    bool sign = false;   // Load: sign-extending
};

Sym sym_add(const Sym& x, const Sym& y) {
    if (x.kind == Sym::Const && y.kind == Sym::Const) return {Sym::Const, x.c + y.c};
    if (x.kind == Sym::Const && y.kind != Sym::Unknown) { Sym r = y; r.c += x.c; return r; }
    if (y.kind == Sym::Const && x.kind != Sym::Unknown) { Sym r = x; r.c += y.c; return r; }
    return {};
}

}  // namespace

// Generic recovery of computed switch jumps ("mov pc, rX", "add pc, rA, rB",
// "ldr pc, [..]" built from a literal table address and an index register).
// Starts at the guarding "cmp idx, #N" and evaluates forward to the jump.
bool Analysis::resolve_computed_jump(Function& fn, const Insn& in, JumpTable& jt) {
    // Collect the contiguous run of instructions that precede the dispatch.
    std::vector<const Insn*> run;
    uint32_t a = in.addr;
    for (int i = 0; i < 24; ++i) {
        auto it = fn.insns.lower_bound(a);
        if (it == fn.insns.begin()) break;
        --it;
        if (!it->second.insn || it->first + it->second.insn->size != a) break;
        run.push_back(it->second.insn);
        a = it->first;
    }
    // The guard is the last "cmp reg, #imm" (or "cmp reg, rK" with a constant rK) before the dispatch.
    uint32_t bound_imm = 0;
    auto cmp_it = std::find_if(run.begin(), run.end(), [&](const Insn* x) {
        if (x->id != ARM_INS_CMP || x->arm.op_count != 2 || x->arm.operands[0].type != ARM_OP_REG) return false;
        if (x->arm.operands[1].type == ARM_OP_IMM) {
            bound_imm = uint32_t(x->arm.operands[1].imm);
            return true;
        }
        return x->arm.operands[1].type == ARM_OP_REG && x->arm.operands[1].shift.type == ARM_SFT_INVALID &&
               const_reg_before(fn, x->addr, x->arm.operands[1].reg, bound_imm);
    });
    if (cmp_it == run.end()) return false;
    const Insn& cmp = **cmp_it;
    uint32_t count = 0;
    jt.index_reg = op(cmp, 0)->reg;
    // The guard is "cmp ; bhi/bhs default" in the run, or a condition on the dispatch.
    if (!find_bound(fn, in, in.addr, jt.index_reg, count)) {
        // find_bound stops on redefinitions of idx (e.g. "lsls r3, r3, #2"); do it locally.
        int cc = is_cond(in) ? in.arm.cc : ARM_CC_INVALID;
        for (auto r = run.begin(); r != cmp_it && cc == ARM_CC_INVALID; ++r)
            if ((*r)->id == ARM_INS_B && is_cond(**r))
                cc = (*r)->arm.cc == ARM_CC_HI ? ARM_CC_LS : (*r)->arm.cc == ARM_CC_HS ? ARM_CC_LO : ARM_CC_INVALID;
        const uint32_t imm = bound_imm;
        if (cc == ARM_CC_LS) count = imm + 1;
        else if (cc == ARM_CC_LO) count = imm;
        else return false;
        if (count == 0 || count > 4096) return false;
    }

    const bool dbg = std::getenv("ABRECOMP_DEBUG_JT") && std::strtoul(std::getenv("ABRECOMP_DEBUG_JT"), nullptr, 16) == in.addr;
    if (dbg) {
        std::fprintf(stderr, "jt %x: guard at %x on r%u, %u cases\n", in.addr, cmp.addr, jt.index_reg, count);
        for (const Insn* x : run) {
            std::fprintf(stderr, "  %x %s id=%u ops=%u:", x->addr, x->text().c_str(), x->id, x->arm.op_count);
            for (unsigned i = 0; i < x->arm.op_count; ++i)
                std::fprintf(stderr, " [t%d r%d imm%d sh%d/%u]", x->arm.operands[i].type, x->arm.operands[i].reg,
                             int(x->arm.operands[i].imm), x->arm.operands[i].shift.type, x->arm.operands[i].shift.value);
            std::fprintf(stderr, "\n");
        }
    }
    // "lsr g, idx, #k ; cmp g, #N": the guard bounds idx >> k, so idx has (N+1) << k values.
    for (auto r = std::next(cmp_it); r != run.end(); ++r) {
        const Insn& d = **r;
        if (!d.writes_reg(jt.index_reg) && !op_is_reg(d, 0, jt.index_reg)) continue;
        // Thumb: "lsr g, idx, #k" (3 operands); ARM: "g, idx lsr #k" (shift on the source operand).
        unsigned k = 0;
        if (d.id == ARM_INS_LSR && op_is_reg(d, 0, jt.index_reg) && op(d, 1)->type == ARM_OP_REG) {
            if (d.arm.op_count == 3 && op(d, 2)->type == ARM_OP_IMM) k = unsigned(op(d, 2)->imm);
            else if (d.arm.op_count == 2 && op(d, 1)->shift.type == ARM_SFT_LSR) k = op(d, 1)->shift.value;
        }
        if (k > 0 && k < 8) {
            // The index register must not change between the shift and the dispatch.
            const unsigned idx = op(d, 1)->reg;
            bool clobbered = false;
            for (auto q = run.begin(); q != r; ++q)
                if ((*q)->writes_reg(idx) && (*q)->id != ARM_INS_CMP) clobbered = true;
            if (!clobbered && (count << k) <= 4096) {
                count <<= k;
                jt.index_reg = idx;
            }
        }
        break;
    }

    Sym regs[ARM_REG_ENDING]{};
    auto get = [&](unsigned r, const Insn& at) -> Sym {
        if (r == ARM_REG_PC) return {Sym::Const, pc_of(at)};
        return r < ARM_REG_ENDING ? regs[r] : Sym{};
    };
    auto shifted = [&](const cs_arm_op& o, const Insn& at) -> Sym {
        Sym v = get(o.reg, at);
        if (o.shift.type == ARM_SFT_INVALID || o.shift.value == 0) return v;
        if (o.shift.type != ARM_SFT_LSL) return {};
        if (v.kind == Sym::Const) return {Sym::Const, v.c << o.shift.value};
        if (v.kind == Sym::Idx && v.c == 0) return {Sym::Idx, 0, v.scale << o.shift.value};
        return {};
    };
    auto load = [&](const Sym& addr, uint8_t width, bool sign) -> Sym {
        if (addr.kind == Sym::Const) {
            if (!image_.contains(addr.c, width)) return {};
            uint32_t v = width == 4 ? image_.read32(addr.c) : width == 2 ? image_.read16(addr.c) : image_.read8(addr.c);
            return {Sym::Const, v};
        }
        if (addr.kind == Sym::Idx) return {Sym::Load, 0, addr.scale, addr.c, width, sign};
        return {};
    };

    std::vector<const Insn*> body(run.rbegin(), run.rend());  // oldest first
    body.push_back(&in);
    for (const Insn* p : body) {
        const Insn& x = *p;
        const bool last = p == &in;
        if (p == &cmp) {
            regs[jt.index_reg] = {Sym::Idx, 0, 1};  // from here on the index is the switch value
            continue;
        }
        Sym result{};
        unsigned dst = op(x, 0) && op(x, 0)->type == ARM_OP_REG ? unsigned(op(x, 0)->reg) : 0;
        switch (x.id) {
            case ARM_INS_B:
                continue;  // the guard branch
            case ARM_INS_BX:
                if (!last || !op(x, 0) || op(x, 0)->type != ARM_OP_REG) return false;
                result = get(op(x, 0)->reg, x);
                dst = ARM_REG_PC;
                break;
            case ARM_INS_MOV:
                if (op(x, 1)->type == ARM_OP_IMM) result = {Sym::Const, uint32_t(op(x, 1)->imm)};
                else result = shifted(*op(x, 1), x);
                break;
            case ARM_INS_ADR:  // Align(PC, 4) + imm
                if (op(x, 1) && op(x, 1)->type == ARM_OP_IMM)
                    result = {Sym::Const, (pc_of(x) & ~3u) + uint32_t(op(x, 1)->imm)};
                break;
            case ARM_INS_LSL:
                if (x.arm.op_count == 2 && op(x, 1)->type == ARM_OP_REG && op(x, 1)->shift.type != ARM_SFT_INVALID) {
                    // ARM "lsl rd, rm, #k": rm with a shift (Thumb "lsls rd, rm" shifts rd by rm)
                    result = shifted(*op(x, 1), x);
                } else if (x.arm.op_count == 3 && op(x, 2)->type == ARM_OP_IMM) {
                    Sym v = get(op(x, 1)->reg, x);
                    unsigned k = unsigned(op(x, 2)->imm);
                    if (v.kind == Sym::Idx && v.c == 0) result = {Sym::Idx, 0, v.scale << k};
                    else if (v.kind == Sym::Const) result = {Sym::Const, v.c << k};
                }
                break;
            case ARM_INS_ADD:
                if (x.arm.op_count == 2) {  // Thumb "add rd, rm" (incl. "add rd, pc")
                    result = sym_add(get(dst, x), shifted(*op(x, 1), x));
                } else if (op(x, 2)->type == ARM_OP_IMM) {
                    Sym v = get(op(x, 1)->reg, x);
                    if (op(x, 1)->reg == ARM_REG_PC) v.c &= ~3u;  // adr uses Align(PC, 4)
                    result = sym_add(v, Sym{Sym::Const, uint32_t(op(x, 2)->imm)});
                } else {
                    result = sym_add(get(op(x, 1)->reg, x), shifted(*op(x, 2), x));
                }
                break;
            case ARM_INS_LDR: case ARM_INS_LDRH: case ARM_INS_LDRB: case ARM_INS_LDRSH: case ARM_INS_LDRSB: {
                const cs_arm_op* m = op(x, 1);
                if (!m || m->type != ARM_OP_MEM || x.arm.writeback) break;
                Sym addr = get(m->mem.base, x);
                if (m->mem.base == ARM_REG_PC) addr.c &= ~3u;
                if (m->mem.index != ARM_REG_INVALID) {
                    if (m->subtracted) break;
                    cs_arm_op io{};
                    io.reg = m->mem.index;
                    io.shift = m->shift;  // capstone keeps the index shift on the operand
                    addr = sym_add(addr, shifted(io, x));
                } else {
                    addr = sym_add(addr, Sym{Sym::Const, uint32_t(m->mem.disp)});
                }
                const uint8_t w = x.id == ARM_INS_LDR ? 4 : (x.id == ARM_INS_LDRH || x.id == ARM_INS_LDRSH) ? 2 : 1;
                result = load(addr, w, x.id == ARM_INS_LDRSH || x.id == ARM_INS_LDRSB);
                break;
            }
            default:
                if (last) return false;
                // Unknown instruction: invalidate every register it writes.
                for (uint8_t i = 0; i < x.regs_write_count; ++i)
                    if (x.regs_write[i] < ARM_REG_ENDING) regs[x.regs_write[i]] = {};
                if (dst && dst < ARM_REG_ENDING && dst != ARM_REG_PC) regs[dst] = {};
                continue;
        }
        if (last) {
            if (dst != ARM_REG_PC) return false;
            if (result.kind == Sym::Idx) {
                // pc = c + idx*scale : table of instructions (e.g. "add pc, pc, idx, lsl #2")
                for (uint32_t i = 0; i < count; ++i) jt.targets.push_back((result.c + i * result.scale) & ~1u);
            } else if (result.kind == Sym::Load) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t ea = result.base + i * result.scale;
                    if (!image_.contains(ea, result.width)) return false;
                    uint32_t v = result.width == 4 ? image_.read32(ea)
                               : result.width == 2 ? image_.read16(ea) : image_.read8(ea);
                    if (result.sign) v = result.width == 2 ? uint32_t(int16_t(v)) : uint32_t(int8_t(v));
                    jt.targets.push_back((v + result.c) & ~1u);
                    for (uint32_t b = ea & ~3u; b < ea + result.width; b += 4) fn.data_words.insert(b);
                }
            } else {
                return false;
            }
            for (uint32_t t : jt.targets)
                if (t < fn.range_begin || t >= fn.range_end) return false;
            jt.kind = JumpTable::Kind::Computed;
            jt.table_addr = result.kind == Sym::Load ? result.base : result.c;
            return true;
        }
        if (dst && dst < ARM_REG_ENDING) regs[dst] = result;
    }
    return false;
}

void Analysis::analyze_function(Function& fn) {
    fn.insns.clear();
    fn.labels.clear();
    fn.jump_tables.clear();
    fn.data_words.clear();
    fn.warnings.clear();
    fn.range_end = exidx_range_end(fn.entry, &fn.range_begin);

    fn.resume_points.clear();
    std::vector<uint32_t> work{fn.entry};
    auto push_label = [&](uint32_t t) {
        fn.labels.insert(t);
        work.push_back(t);
    };
    // Landing pads are only reached through the unwinder: decode them as roots.
    if (fn.entry == fn.range_begin)
        if (auto rp = region_pads_.find(fn.range_begin); rp != region_pads_.end())
            for (uint32_t pad : rp->second)
                if (pad > fn.entry && pad < fn.range_end) {
                    push_label(pad);
                    fn.resume_points.insert(pad);
                }
    while (!work.empty()) {
        uint32_t a = work.back();
        work.pop_back();
        // Follow straight-line code; push branch targets onto the work list.
        while (true) {
            if (fn.insns.count(a)) break;
            auto nb = functions_.find(a);
            if (a != fn.entry && (a >= fn.range_end || a < fn.range_begin ||
                                  (nb != functions_.end() && nb->second.mode == fn.mode))) {
                fn.labels.insert(a);  // falls into another function: lifter emits a tail call
                break;
            }
            const Insn* in = dis_.decode(a, fn.mode);
            if (!in) {
                fn.insns[a] = InsnInfo{nullptr, Flow::Invalid};
                fn.warnings.push_back("undecodable instruction");
                break;
            }
            if (fn.data_words.count(a & ~3u))
                fn.warnings.push_back("code overlaps data word");
            InsnInfo info = classify(fn, *in);

            // Literal pool loads: remember data words (diagnostics; not decoded as code).
            for (uint8_t i = 0; i < in->arm.op_count; ++i) {
                const cs_arm_op& o = in->arm.operands[i];
                if (o.type == ARM_OP_MEM && o.mem.base == ARM_REG_PC && o.mem.index == ARM_REG_INVALID &&
                    info.flow != Flow::JumpTable) {
                    uint32_t lit = (pc_of(*in) & ~3u) + uint32_t(o.mem.disp);
                    fn.data_words.insert(lit & ~3u);
                }
            }
            if (info.flow == Flow::JumpTable) {
                JumpTable jt;
                bool ok = resolve_jump_table(fn, *in, jt);
                if (ok) {
                    resolved_tables_[a] = jt;
                } else if (auto known = resolved_tables_.find(a); known != resolved_tables_.end()) {
                    // A fragment entered after the bounds check: another function resolved this switch.
                    jt = known->second;
                    ok = true;
                }
                if (ok) {
                    for (uint32_t t : jt.targets) push_label(t);
                    fn.jump_tables[a] = std::move(jt);
                } else {
                    info.flow = Flow::IndirectJump;
                    if (in->id != ARM_INS_BX) fn.warnings.push_back("unresolved jump table");
                }
            }
            fn.insns[a] = info;

            const uint32_t next = a + in->size;
            if (info.flow == Flow::Call && is_import_stub(info.target)) {
                const std::string callee = name_of(info.target);
                if (callee == "setjmp" || callee == "_setjmp" || callee == "sigsetjmp") fn.resume_points.insert(next);
            }
            bool falls = true;
            switch (info.flow) {
                case Flow::Branch:
                    push_label(info.target);
                    falls = info.conditional;
                    break;
                case Flow::TailCall:
                    add_entry(info.target, info.target_mode, "tailcall");
                    falls = info.conditional;
                    break;
                case Flow::Call: {
                    add_entry(info.target, info.target_mode, "call");
                    const std::string nm = name_of(info.target);
                    if (noreturn_.count(info.target) || (!nm.empty() && noreturn_names_.count(nm)))
                        falls = info.conditional;
                    break;
                }
                case Flow::Return:
                case Flow::IndirectJump:
                    falls = info.conditional;
                    break;
                case Flow::JumpTable:
                    falls = info.conditional;  // ARM "ldrls pc": falls to default when cond fails
                    break;
                case Flow::Invalid:
                    falls = false;
                    break;
                default:
                    break;
            }
            if (!falls) break;
            if (fn.jump_tables.count(a) && info.flow == Flow::JumpTable && falls) {
                // default path at next; table words follow it and must not be decoded
            }
            a = next;
        }
    }
}

void Analysis::compute_noreturn() {
    // A function is noreturn if no reachable exit leaves it normally.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [entry, fn] : functions_) {
            if (noreturn_.count(entry)) continue;
            if (is_import_stub(entry)) continue;
            bool exits = false;
            for (const auto& [a, info] : fn.insns) {
                if (info.flow == Flow::Return || info.flow == Flow::IndirectJump ||
                    (info.flow == Flow::TailCall && !noreturn_.count(info.target))) {
                    exits = true;
                    break;
                }
            }
            for (uint32_t l : fn.labels)
                if (!fn.insns.count(l)) exits = true;  // falls into a neighbour
            if (!exits && !fn.insns.empty()) {
                noreturn_.insert(entry);
                changed = true;
            }
        }
    }
    for (auto& [entry, fn] : functions_) fn.noreturn = noreturn_.count(entry) != 0;
}

void Analysis::run() {
    parse_exidx();
    parse_lsda();
    seed_entries();
    // PLT stubs are not recompiled; they are import thunks.
    auto drain = [&] {
        while (!pending_.empty()) {
            auto [addr, mode_why] = *pending_.begin();
            auto [mode, why] = mode_why;
            pending_.erase(pending_.begin());
            if (functions_.count(addr)) continue;
            if (is_import_stub(addr)) {
                Function& stub = functions_[addr];
                stub.entry = addr;
                stub.mode = Mode::Arm;
                continue;
            }
            Function& fn = functions_[addr];
            fn.entry = addr;
            fn.mode = mode;
            fn.origin = why;
            analyze_function(fn);
        }
    };
    drain();
    // Remaining exidx starts: every one is a function entry.
    std::unordered_map<uint32_t, Mode> seen_mode;
    for (const auto& [e, fn] : functions_)
        for (const auto& [a, info] : fn.insns) seen_mode.emplace(a, fn.mode);
    for (const auto& e : exidx_) {
        if (functions_.count(e.start) || e.start >= image_.text_end()) continue;
        auto it = seen_mode.find(e.start);
        add_entry(e.start, it != seen_mode.end() ? it->second : guess_mode(e.start),
                  it != seen_mode.end() ? "exidx-seen" : "exidx");
    }
    drain();
    // Functions reached only through pointers computed at run time, sharing the
    // exidx entry of the function before them: an ARM "push {..., lr}" in an ARM
    // region that no decoded function covers, or an ARM "bx lr" there (an empty
    // function, such as a do-nothing member function).
    seen_mode.clear();
    for (const auto& [e, fn] : functions_)
        for (const auto& [a, info] : fn.insns) seen_mode.emplace(a, fn.mode);
    for (size_t i = 0; i < exidx_.size(); ++i) {
        const uint32_t begin = exidx_[i].start;
        auto f = functions_.find(begin);
        if (f == functions_.end() || f->second.mode != Mode::Arm) continue;
        const uint32_t end = std::min(exidx_range_end(begin, nullptr), image_.text_end());
        for (uint32_t a = (begin + 4) & ~3u; a + 4 <= end; a += 4)
            if (!seen_mode.count(a) && !functions_.count(a)) {
                const uint32_t insn = image_.read32(a);
                if ((insn & 0xffff4000u) == 0xe92d4000u) add_entry(a, Mode::Arm, "prologue");
                else if (insn == 0xe12fff1eu) add_entry(a, Mode::Arm, "leaf");
            }
    }
    drain();
    for (int pass = 0; pass < 4; ++pass) {
        // Code pointers formed in code; new functions can contain more.
        for (bool first = true;; first = false) {
            insn_owner_.clear();
            for (const auto& [e, fn] : functions_)
                for (const auto& [a, info] : fn.insns) insn_owner_.emplace(a, e);
            // Even code pointers in data: ARM functions unless they point into a
            // known body (labels of computed-goto tables, for instance). Rejected
            // ones are checked again once function bodies are final.
            if (first) {
                std::vector<uint32_t> refs = std::move(even_code_refs_);
                even_code_refs_.clear();
                if (pass > 0) refs.insert(refs.end(), rejected_refs_.begin(), rejected_refs_.end());
                rejected_refs_.clear();
                for (uint32_t v : refs) {
                    if (functions_.count(v) || (v & 3)) continue;
                    if (insn_owner_.count(v)) {
                        rejected_refs_.push_back(v);
                        continue;
                    }
                    // Thumb function pointers are odd, so only ARM is in question: accept
                    // code that looks more like ARM than Thumb, or is plausible ARM anyway
                    // (runs of "b" thunks score badly in the linear scan).
                    if (guess_mode(v) == Mode::Arm || mode_score(v, Mode::Arm, true) > -8) add_entry(v, Mode::Arm, pass ? "ref-recheck" : "reloc-even");
                }
            }
            for (const auto& [e, fn] : functions_)
                if (scanned_.insert(e).second) scan_code_pointers(fn);
            if (pending_.empty()) break;
            drain();
        }
        insn_owner_.clear();

        // Iterate: new entries can turn branches into tail calls, and noreturn
        // inference can cut fallthroughs after calls.
        for (int round = 0; round < 8; ++round) {
            const size_t before = noreturn_.size();
            compute_noreturn();
            bool any = noreturn_.size() != before;
            for (auto& [entry, fn] : functions_) {
                if (is_import_stub(entry)) continue;
                bool stale = false;
                for (const auto& [a, info] : fn.insns) {
                    // Runs into another entry of the same mode: that is a tail call now.
                    auto other = a != entry ? functions_.find(a) : functions_.end();
                    if ((other != functions_.end() && other->second.mode == fn.mode) ||
                        (info.flow == Flow::IndirectJump && resolved_tables_.count(a)) ||
                        (info.flow == Flow::Branch && info.target != entry && functions_.count(info.target) &&
                         functions_.at(info.target).mode == fn.mode) ||
                        (any && info.flow == Flow::Call && noreturn_.count(info.target) && !info.conditional &&
                         fn.insns.count(a + info.insn->size))) {
                        stale = true;
                        break;
                    }
                }
                if (stale) {
                    analyze_function(fn);
                    any = true;
                }
            }
            drain();
            if (!any) break;
        }
        if (rejected_refs_.empty()) break;
        // Bodies may have shrunk (noreturn calls): rejected pointers get another look.
        scanned_.clear();
    }
    compute_noreturn();
    prune_unreachable();
    find_frame_poppers();
}

void Analysis::find_frame_poppers() {
    auto has_reg = [](const Insn& in, unsigned first, arm_reg r) {
        for (unsigned i = first; i < in.arm.op_count; ++i)
            if (op_is_reg(in, i, r)) return true;
        return false;
    };
    for (auto& [entry, fn] : functions_) {
        if (is_import_stub(entry)) continue;
        bool pushes_lr = false, pops_pc = false;
        for (const auto& [a, info] : fn.insns) {
            if (!info.insn) continue;
            const Insn& in = *info.insn;
            switch (in.id) {
                case ARM_INS_PUSH:
                    pushes_lr |= has_reg(in, 0, ARM_REG_LR);
                    break;
                case ARM_INS_STMDB:
                    pushes_lr |= op_is_reg(in, 0, ARM_REG_SP) && has_reg(in, 1, ARM_REG_LR);
                    break;
                case ARM_INS_STR:
                    // "str lr, [sp, #-4]!"
                    pushes_lr |= op_is_reg(in, 0, ARM_REG_LR) && in.arm.op_count > 1 &&
                                 in.arm.operands[1].type == ARM_OP_MEM && in.arm.operands[1].mem.base == ARM_REG_SP;
                    break;
                case ARM_INS_POP: case ARM_INS_LDM:
                    pops_pc |= info.flow == Flow::Return;
                    break;
                default:
                    break;
            }
        }
        fn.pops_caller_frame = pops_pc && !pushes_lr;
        if (fn.pops_caller_frame && std::getenv("ABRECOMP_DEBUG_FRAMES"))
            std::fprintf(stderr, "frame popper: %08x\n", entry);
    }
}

// Re-analysis can drop the call that created an entry; such entries (often
// wrong-mode garbage) are removed unless something still reaches them.
void Analysis::prune_unreachable() {
    std::unordered_set<uint32_t> live;
    std::vector<uint32_t> work(roots_.begin(), roots_.end());
    while (!work.empty()) {
        uint32_t a = work.back();
        work.pop_back();
        auto it = functions_.find(a);
        if (it == functions_.end() || !live.insert(a).second) continue;
        for (const auto& [ia, info] : it->second.insns)
            if (info.flow == Flow::Call || info.flow == Flow::TailCall) work.push_back(info.target);
        for (uint32_t l : it->second.labels)
            if (!it->second.insns.count(l)) work.push_back(l);
    }
    for (auto it = functions_.begin(); it != functions_.end();)
        it = live.count(it->first) ? std::next(it) : functions_.erase(it);
}

}  // namespace recomp
