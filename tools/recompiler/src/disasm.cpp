// SPDX-License-Identifier: GPL-3.0-or-later
#include "disasm.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace recomp {

namespace {
// At least two operand slots, zeroed when unused.
size_t arm_size(const cs_arm& d) { return offsetof(cs_arm, operands) + std::max<size_t>(d.op_count, 2) * sizeof(cs_arm_op); }
}  // namespace

Insn::Insn(const cs_arm& detail)
    : arm_size_(arm_size(detail)),
      arm_storage_(new unsigned char[arm_size_]()),
      arm(*reinterpret_cast<const cs_arm*>(arm_storage_.get())) {
    std::memcpy(arm_storage_.get(), &detail, offsetof(cs_arm, operands) + detail.op_count * sizeof(cs_arm_op));
}

void Insn::replace_with(const Insn& o) {
    if (o.arm_size_ != arm_size_) throw std::runtime_error("capstone: instruction decoded with other operands");
    addr = o.addr, size = o.size, mode = o.mode, id = o.id, name = o.name;
    mnemonic = o.mnemonic, op_str = o.op_str, raw = o.raw;
    std::copy_n(o.regs_read, 20, regs_read), regs_read_count = o.regs_read_count;
    std::copy_n(o.regs_write, 20, regs_write), regs_write_count = o.regs_write_count;
    std::copy_n(o.groups, 8, groups), groups_count = o.groups_count;
    std::memcpy(arm_storage_.get(), o.arm_storage_.get(), arm_size_);
}

Disassembler::Disassembler(const ElfImage& image) : image_(image) {
    if (cs_open(CS_ARCH_ARM, CS_MODE_ARM, &arm_) != CS_ERR_OK ||
        cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &thumb_) != CS_ERR_OK)
        throw std::runtime_error("capstone: cs_open failed");
    for (csh h : {arm_, thumb_}) cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
    scratch_arm_ = cs_malloc(arm_);
    scratch_thumb_ = cs_malloc(thumb_);
}

Disassembler::~Disassembler() {
    cs_free(scratch_arm_, 1);
    cs_free(scratch_thumb_, 1);
    cs_close(&arm_);
    cs_close(&thumb_);
}

// Capstone tracks Thumb IT-block state inside the handle, so instructions of
// an IT block must be decoded right after their IT, in order. decode() does
// that eagerly and caches the whole block; afterwards the handle state is
// clean again (or the handle is reopened if a block member failed to decode).
const Insn* Disassembler::decode(uint32_t addr, Mode mode) {
    const uint64_t key = uint64_t(addr) << 1 | uint64_t(mode);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second.get();
    const Insn* insn = decode_uncached(addr, mode);
    if (insn && insn->id == ARM_INS_IT) {
        const unsigned mask = insn->raw & 0xf;
        const unsigned count = 4 - unsigned(__builtin_ctz(mask));
        uint32_t a = addr + insn->size;
        for (unsigned i = 0; i < count; ++i) {
            const Insn* member = decode_uncached(a, mode);
            if (!member) {
                reset_thumb();
                break;
            }
            a += member->size;
        }
    }
    return insn;
}

void Disassembler::reset_thumb() {
    cs_free(scratch_thumb_, 1);
    cs_close(&thumb_);
    if (cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &thumb_) != CS_ERR_OK)
        throw std::runtime_error("capstone: cs_open failed");
    cs_option(thumb_, CS_OPT_DETAIL, CS_OPT_ON);
    scratch_thumb_ = cs_malloc(thumb_);
}

const Insn* Disassembler::decode_uncached(uint32_t addr, Mode mode) {
    const uint64_t key = uint64_t(addr) << 1 | uint64_t(mode);

    std::unique_ptr<Insn> result;
    const unsigned align = mode == Mode::Thumb ? 2 : 4;
    if (addr % align == 0 && image_.contains(addr, 2)) {
        csh h = mode == Mode::Thumb ? thumb_ : arm_;
        cs_insn* ci = mode == Mode::Thumb ? scratch_thumb_ : scratch_arm_;
        auto bytes = image_.bytes(addr, std::min<uint32_t>(4, image_.image_end() - addr));
        const uint8_t* code = bytes.data();
        size_t size = bytes.size();
        uint64_t address = addr;
        if (cs_disasm_iter(h, &code, &size, &address, ci)) {
            result = std::make_unique<Insn>(ci->detail->arm);
            Insn& in = *result;
            in.addr = addr;
            in.size = uint8_t(ci->size);
            in.mode = mode;
            in.id = ci->id;
            in.name = cs_insn_name(h, ci->id);
            in.mnemonic = ci->mnemonic;
            in.op_str = ci->op_str;
            in.regs_read_count = ci->detail->regs_read_count;
            in.regs_write_count = ci->detail->regs_write_count;
            in.groups_count = ci->detail->groups_count;
            std::copy_n(ci->detail->regs_read, in.regs_read_count, in.regs_read);
            std::copy_n(ci->detail->regs_write, in.regs_write_count, in.regs_write);
            std::copy_n(ci->detail->groups, in.groups_count, in.groups);
            if (in.size == 4 && mode == Mode::Thumb)
                in.raw = uint32_t(image_.read16(addr)) << 16 | image_.read16(addr + 2);
            else
                in.raw = in.size == 4 ? image_.read32(addr) : image_.read16(addr);
        }
    }
    auto& slot = cache_[key];
    if (slot && result) {
        slot->replace_with(*result);  // re-decoded in IT context: update in place
    } else if (!slot) {
        slot = std::move(result);
    }
    return slot.get();
}

}  // namespace recomp
