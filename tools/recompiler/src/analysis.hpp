// SPDX-License-Identifier: GPL-3.0-or-later
// Function discovery and per-function control-flow recovery.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "disasm.hpp"
#include "elf_image.hpp"

namespace recomp {

// How an instruction affects control flow.
enum class Flow : uint8_t {
    Normal,        // falls through
    Branch,        // direct intra-function branch (maybe conditional)
    TailCall,      // direct branch to another function (maybe conditional)
    Call,          // direct call (BL/BLX imm)
    CallIndirect,  // BLX reg
    Return,        // bx lr / pop {pc} / ... (maybe conditional)
    IndirectJump,  // unresolved computed jump: goes through dispatch
    JumpTable,     // resolved switch
    Invalid,       // undecodable: becomes a trap
};

struct InsnInfo {
    const Insn* insn = nullptr;  // nullptr for Invalid
    Flow flow = Flow::Normal;
    bool conditional = false;
    uint32_t target = 0;         // Branch/TailCall/Call target address (no Thumb bit)
    Mode target_mode = Mode::Arm;
};

struct JumpTable {
    enum class Kind : uint8_t { Tbb, Tbh, LdrPc, AddPcBranches, Computed };
    Kind kind{};
    unsigned index_reg = 0;  // ARM_REG_*
    uint32_t table_addr = 0;
    std::vector<uint32_t> targets;  // indexed by case value
};

struct Function {
    uint32_t entry = 0;
    Mode mode = Mode::Arm;
    uint32_t range_begin = 0, range_end = 0;  // exidx region that contains the entry
    std::map<uint32_t, InsnInfo> insns;       // reachable instructions, by address
    std::set<uint32_t> labels;                // intra-function branch targets
    std::map<uint32_t, JumpTable> jump_tables;  // by address of the dispatching insn
    std::set<uint32_t> data_words;            // literal-pool / table words inside the body
    std::vector<std::string> warnings;
    bool noreturn = false;
    const char* origin = "";  // why this entry was created (diagnostics)
    // Where a non-local jump can resume this function: C++ landing pads (from
    // the LSDA) and the return addresses of setjmp calls.
    std::set<uint32_t> resume_points;
    // Returns by popping PC from a frame it never pushed: the caller's (the
    // libgcc soft-float "bl Lml_s" idiom). Such a "call" returns to the
    // caller's caller, and the emitter checks for that after the call.
    bool pops_caller_frame = false;
};

struct ExidxEntry {
    uint32_t start = 0;
    uint32_t data = 0;   // raw second word
    bool cant_unwind = false;
};

class Analysis {
public:
    Analysis(const ElfImage& image, Disassembler& dis);

    void run();
    // Exposed for validation against symbols with known mode.
    Mode guess_mode(uint32_t addr) const;
    // Plausibility of decoding at addr in mode m (see guess_mode). path_only stops
    // the linear scan at the first unconditional branch.
    double mode_score(uint32_t addr, Mode m, bool path_only = false) const;

    const std::map<uint32_t, Function>& functions() const { return functions_; }
    const std::vector<ExidxEntry>& exidx() const { return exidx_; }
    // Name for a function entry (dynsym or import), empty if unknown.
    std::string name_of(uint32_t addr) const;
    bool is_import_stub(uint32_t addr) const { return image_.plt_imports().count(addr) != 0; }

private:
    void parse_exidx();
    void parse_lsda();
    void seed_entries();
    void add_entry(uint32_t addr, Mode mode, const char* why);
    void analyze_function(Function& fn);
    InsnInfo classify(const Function& fn, const Insn& in);
    bool resolve_jump_table(Function& fn, const Insn& in, JumpTable& out);
    bool resolve_computed_jump(Function& fn, const Insn& in, JumpTable& out);
    bool resolve_offset_table(Function& fn, const Insn& in, JumpTable& out);
    bool find_bound(const Function& fn, const Insn& dispatch, uint32_t before, unsigned reg, uint32_t& bound) const;
    bool const_reg_before(const Function& fn, uint32_t addr, unsigned reg, uint32_t& value) const;
    bool literal_reg_before(const Function& fn, uint32_t addr, unsigned reg, uint32_t& value) const;
    void scan_code_pointers(const Function& fn);
    bool find_branch_guard(const Function& fn, uint32_t table_insn, unsigned reg, uint32_t& count) const;
    uint32_t exidx_range_end(uint32_t addr, uint32_t* begin) const;
    void compute_noreturn();
    void prune_unreachable();
    void find_frame_poppers();

    const ElfImage& image_;
    Disassembler& dis_;
    std::vector<ExidxEntry> exidx_;
    std::vector<uint32_t> exidx_starts_sorted_;
    std::map<uint32_t, std::set<uint32_t>> region_pads_;  // exidx region start -> landing pads
    std::map<uint32_t, Function> functions_;
    std::map<uint32_t, std::pair<Mode, const char*>> pending_;
    std::unordered_map<uint32_t, std::string> names_;
    std::unordered_set<uint32_t> noreturn_;
    std::unordered_set<uint32_t> roots_;  // entries with external evidence
    std::vector<uint32_t> even_code_refs_;  // even .text addresses in data, checked after the first pass
    std::vector<uint32_t> rejected_refs_;   // code pointers that pointed into a function body
    std::map<uint32_t, JumpTable> resolved_tables_;  // by dispatch address, shared with fragments
    std::unordered_set<uint32_t> scanned_;  // functions searched for code pointers
    std::unordered_map<uint32_t, uint32_t> insn_owner_;  // insn address -> function (for scan_code_pointers)
    std::unordered_set<std::string> noreturn_names_;
};

const char* flow_name(Flow f);

}  // namespace recomp
