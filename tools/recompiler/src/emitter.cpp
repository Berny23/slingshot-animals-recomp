// SPDX-License-Identifier: GPL-3.0-or-later
#include "emitter.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace recomp {
namespace {

std::string hex(uint32_t v) { return Lifter::hex(v); }

std::string label(uint32_t a) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "L_%x", a);
    return buf;
}

bool write_file(const std::filesystem::path& p, const std::string& s, std::string& error) {
    // Only rewrite when content changed, so incremental builds stay incremental.
    std::ifstream in(p, std::ios::binary);
    if (in) {
        std::string old((std::istreambuf_iterator<char>(in)), {});
        if (old == s) return true;
    }
    std::ofstream out(p, std::ios::binary);
    if (!out) {
        error = "cannot write " + p.string();
        return false;
    }
    out << s;
    return true;
}

// Guest functions replaced by runtime code (see runtime/core/src/unwind.cpp):
// symbol name -> hook name.
const std::map<std::string, std::string> kHooks = {
    {"restore_core_regs", "restore_core_regs"},
    {"__restore_core_regs", "restore_core_regs"},
};

std::string sanitize_comment(std::string s) {
    for (size_t i = 0; (i = s.find("*/", i)) != std::string::npos;) s.replace(i, 2, "* /");
    return s;
}

}  // namespace

std::string Emitter::fn_name(uint32_t addr) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "fn_%08x", addr);
    return buf;
}

std::string Emitter::original_name(uint32_t addr) { return fn_name(addr) + "_original"; }

// Direct call or tail call to `target`.
std::string Emitter::call_code(const Function& fn, uint32_t target, Mode mode, bool tail) const {
    (void)fn;
    if (auto it = import_index_.find(target); it != import_index_.end()) {
        std::string c = "recomp::call_import(ctx, " + std::to_string(it->second) + "); /* " + an_.name_of(target) + " */ ";
        return tail ? c + "return; " : c;
    }
    auto f = an_.functions().find(target);
    if (f != an_.functions().end() && f->second.mode == mode && !an_.is_import_stub(target)) {
        if (tail && in_try_) return fn_name(target) + "(ctx); return; ";
        if (tail) return "RECOMP_MUSTTAIL return " + fn_name(target) + "(ctx); ";
        return fn_name(target) + "(ctx); ";
    }
    // Unknown target: resolve at runtime (reports an error if it does not exist).
    const uint32_t t = target | (mode == Mode::Thumb ? 1u : 0u);
    if (tail) return "recomp::jump_indirect(ctx, " + hex(t) + "); return; ";
    return "recomp::call_indirect(ctx, " + hex(t) + "); ";
}

std::string Emitter::fallthrough_code(const Function& fn, uint32_t next) const {
    if (fn.insns.count(next)) return "goto " + label(next) + "; ";
    return call_code(fn, next, fn.mode, true);
}

std::string Emitter::emit_function(const Function& fn, EmitStats& stats, const EmitOptions& opt) {
    std::ostringstream o;
    const std::string name = an_.name_of(fn.entry);
    o << "// " << (fn.mode == Mode::Thumb ? "thumb" : "arm") << (name.empty() ? "" : " " + sanitize_comment(name)) << "\n";
    std::string def_name = fn_name(fn.entry);
    if (auto w = opt.wraps.find(fn.entry); w != opt.wraps.end()) {
        // Every call goes to the hook; the code below becomes fn_XXXXXXXX_original.
        o << "void " << def_name << "(recomp::CPU* __restrict ctx) {\n"
          << "    static const recomp::GuestFunc hook = recomp::find_hook(\"" << w->second << "\");\n"
          << "    hook(ctx);\n}\n\n";
        def_name = original_name(fn.entry);
    }
    o << "void " << def_name << "(recomp::CPU* __restrict ctx) {\n";
    if (auto h = kHooks.find(name); h != kHooks.end()) {
        o << "    static const recomp::GuestFunc hook = recomp::find_hook(\"" << h->second << "\");\n"
          << "    hook(ctx);\n}\n\n";
        return o.str();
    }

    // Functions that a non-local jump (C++ exception, longjmp) can resume run
    // inside a try block; the runtime throws GuestUnwind{pc, sp} and the frame
    // whose last call was made with that sp continues at the resume point.
    in_try_ = !fn.resume_points.empty();
    if (in_try_) {
        o << "    uint32_t eh_sp = 0, eh_resume = 0;\n";
    }

    bool has_indirect = false;
    for (const auto& [a, info] : fn.insns)
        if (info.flow == Flow::IndirectJump || info.flow == Flow::JumpTable) has_indirect = true;
    if (has_indirect) o << "    const uint32_t entry_lr = ctx->r[14];\n";
    if (in_try_) {
        o << "    for (;;) try {\n    switch (eh_resume) {\n";
        for (uint32_t p : fn.resume_points)
            if (fn.insns.count(p)) o << "        case " << hex(p) << ": goto " << label(p) << ";\n";
        o << "        default: break;\n    }\n";
    }
    if (fn.insns.empty() || fn.insns.begin()->first != fn.entry) o << "    goto " << label(fn.entry) << ";\n";

    for (auto it = fn.insns.begin(); it != fn.insns.end(); ++it) {
        const uint32_t a = it->first;
        const InsnInfo& info = it->second;
        o << label(a) << ": ";
        if (!info.insn) {
            o << "recomp::trap(ctx, " << hex(a) << ", \"undecodable instruction\");\n";
            continue;
        }
        const Insn& in = *info.insn;
        ++stats.instructions;
        o << "/* " << std::hex << a << std::dec << ": " << sanitize_comment(in.text()) << " */\n    ";

        LiftResult lr = lifter_.lift(in);
        if (lr.unimplemented) {
            ++stats.unimplemented;
            stats.unimplemented_by_mnemonic[std::string(in.mode == Mode::Thumb ? "T:" : "A:") + in.name]++;
        }

        std::string cond;
        if (in.id == ARM_INS_CBZ || in.id == ARM_INS_CBNZ)
            cond = "ctx->r[" + std::to_string(Lifter::gpr(in.arm.operands[0].reg)) + "] " +
                   (in.id == ARM_INS_CBZ ? "== 0" : "!= 0");
        else if (info.conditional)
            cond = lifter_.cond_expr(in.arm.cc);

        const uint32_t ret_addr = (a + in.size) | (in.mode == Mode::Thumb ? 1u : 0u);
        std::string flow;
        switch (info.flow) {
            case Flow::Normal:
                if (!lr.new_pc.empty()) flow = "recomp::jump_indirect(ctx, " + lr.new_pc + "); return; ";
                break;
            case Flow::Branch:
                flow = "goto " + label(info.target) + "; ";
                break;
            case Flow::TailCall:
                flow = call_code(fn, info.target, info.target_mode, true);
                break;
            case Flow::Call: {
                flow = std::string(in_try_ ? "eh_sp = ctx->r[13]; " : "") + "ctx->r[14] = " + hex(ret_addr) + "; ";
                auto callee = an_.functions().find(info.target);
                if (callee != an_.functions().end() && callee->second.pops_caller_frame) {
                    // The callee may return straight to this function's caller (see Function::pops_caller_frame).
                    flow += "ctx->r[15] = " + hex(ret_addr) + "; " + call_code(fn, info.target, info.target_mode, false) +
                            "if (ctx->r[15] != " + hex(ret_addr) + ") return; ";
                } else {
                    flow += call_code(fn, info.target, info.target_mode, false);
                }
                break;
            }
            case Flow::CallIndirect:
                flow = std::string(in_try_ ? "eh_sp = ctx->r[13]; " : "") + "ctx->r[14] = " + hex(ret_addr) +
                       "; recomp::call_indirect(ctx, " + lr.new_pc + "); ";
                break;
            case Flow::Return:
                flow = fn.pops_caller_frame && lr.new_pc == "tgt" ? "ctx->r[15] = tgt; return; " : "return; ";
                break;
            case Flow::IndirectJump:
                flow = "if (" + lr.new_pc + " == entry_lr) return; ";
                // An unbounded computed jump ("add pc, rBase, rOff", computed goto):
                // it may land on any instruction of this function. ("bx rN" is nearly
                // always a tail call through a function pointer.)
                if (in.id != ARM_INS_BX) {
                    flow += "switch (" + lr.new_pc + " & ~1u) { ";
                    for (const auto& [ia, ii] : fn.insns)
                        if (ii.insn) flow += "case " + hex(ia) + ": goto " + label(ia) + "; ";
                    flow += "default: break; } ";
                }
                flow += "recomp::jump_indirect(ctx, " + lr.new_pc + "); return; ";
                break;
            case Flow::JumpTable: {
                auto jt = fn.jump_tables.find(a);
                flow = "switch (" + lr.new_pc + " & ~1u) { ";
                if (jt != fn.jump_tables.end()) {
                    std::set<uint32_t> seen;
                    for (uint32_t t : jt->second.targets)
                        if (seen.insert(t).second) {
                            flow += "case " + hex(t) + ": ";
                            flow += fn.insns.count(t) ? "goto " + label(t) + "; " : call_code(fn, t, fn.mode, true);
                        }
                }
                flow += "} if (" + lr.new_pc + " == entry_lr) return; recomp::jump_indirect(ctx, " + lr.new_pc + "); return; ";
                break;
            }
            case Flow::Invalid:
                flow = "recomp::trap(ctx, " + hex(a) + ", \"invalid\"); ";
                break;
        }

        const bool needs_pc = info.flow == Flow::CallIndirect || info.flow == Flow::IndirectJump || info.flow == Flow::JumpTable;
        if (needs_pc && lr.new_pc.empty()) flow = "recomp::trap(ctx, " + hex(a) + ", \"no jump target\"); ";
        const std::string body = lr.code + flow;
        if (!body.empty()) {
            if (cond.empty()) o << "{ " << body << "}\n";
            else o << "if (" << cond << ") { " << body << "}\n";
        } else {
            o << ";\n";
        }

        // Fallthrough to the next instruction if it is not emitted right after this one.
        bool falls = true;
        switch (info.flow) {
            case Flow::Branch: case Flow::TailCall: case Flow::Return: case Flow::IndirectJump: case Flow::JumpTable:
                falls = info.conditional || in.id == ARM_INS_CBZ || in.id == ARM_INS_CBNZ;
                break;
            case Flow::Call:
                falls = !(an_.functions().count(info.target) && an_.functions().at(info.target).noreturn) || info.conditional;
                break;
            case Flow::Invalid:
                falls = false;
                break;
            default:
                if (!lr.new_pc.empty() && !info.conditional) falls = false;
                break;
        }
        if (falls) {
            const uint32_t next = a + in.size;
            auto nx = std::next(it);
            if (nx == fn.insns.end() || nx->first != next) o << "    " << fallthrough_code(fn, next) << "\n";
        }
    }
    if (in_try_) {
        o << "    return;\n    } catch (const recomp::GuestUnwind& u) {\n"
          << "        if (u.sp != eh_sp) throw;\n        switch (u.pc) {\n";
        for (uint32_t p : fn.resume_points)
            if (fn.insns.count(p)) o << "            case " << hex(p) << ":\n";
        o << "                eh_resume = u.pc;\n                break;\n            default: throw;\n        }\n    }\n";
        in_try_ = false;
    }
    o << "}\n\n";
    return o.str();
}

bool Emitter::emit(const EmitOptions& opt, EmitStats& stats, std::string& error) {
    namespace fs = std::filesystem;
    fs::create_directories(opt.out_dir);

    // Import table: one index per distinct imported name.
    for (const auto& [stub, name] : image_.plt_imports()) import_names_.emplace(name, 0);
    unsigned idx = 0;
    for (auto& [name, i] : import_names_) i = idx++;
    for (const auto& [stub, name] : image_.plt_imports()) import_index_[stub] = import_names_[name];

    std::vector<const Function*> fns;
    for (const auto& [e, fn] : an_.functions())
        if (!an_.is_import_stub(e)) fns.push_back(&fn);

    // Declarations.
    std::ostringstream h;
    h << "// Generated by abrecomp. Do not edit.\n#pragma once\n#include \"recomp/cpu.hpp\"\n\n";
    for (const Function* f : fns) h << "void " << fn_name(f->entry) << "(recomp::CPU* __restrict ctx);\n";
    for (const auto& [addr, hook] : opt.wraps) {
        if (!an_.functions().count(addr)) {
            error = "--wrap: no function at " + hex(addr);
            return false;
        }
        h << "void " << original_name(addr) << "(recomp::CPU* __restrict ctx);\n";
    }
    if (!write_file(fs::path(opt.out_dir) / "funcs.hpp", h.str(), error)) return false;

    // Function bodies, split evenly into a fixed number of files.
    const size_t per_file = (fns.size() + opt.file_count - 1) / opt.file_count;
    size_t file_no = 0;
    for (size_t i = 0; file_no < opt.file_count; i += per_file, ++file_no) {
        std::ostringstream o;
        o << "// Generated by abrecomp. Do not edit.\n#include \"funcs.hpp\"\n\n"
          << "#pragma clang diagnostic ignored \"-Wunused-label\"\n"
          << "#pragma clang diagnostic ignored \"-Wunused-variable\"\n\n";
        for (size_t j = i; j < std::min(fns.size(), i + per_file); ++j) {
            o << emit_function(*fns[j], stats, opt);
            ++stats.functions;
        }
        char name[64];
        std::snprintf(name, sizeof name, "funcs_%03zu.cpp", file_no);
        if (!write_file(fs::path(opt.out_dir) / name, o.str(), error)) return false;
    }
    stats.files = file_no;

    // Lookup tables.
    std::ostringstream t;
    t << "// Generated by abrecomp. Do not edit.\n#include \"funcs.hpp\"\n#include \"recomp/generated.hpp\"\n\n"
      << "namespace recomp::gen {\n\n"
      << "const char* const lib_sha256 = \"" << opt.lib_sha256 << "\";\n\n"
      << "const FuncEntry functions[] = {\n";
    for (const Function* f : fns)
        t << "    {" << hex(f->entry | (f->mode == Mode::Thumb ? 1u : 0u)) << ", " << fn_name(f->entry) << "},\n";
    t << "};\nconst size_t function_count = " << fns.size() << ";\n\n";
    t << "const OriginalEntry originals[] = {\n";
    for (const auto& [addr, hook] : opt.wraps) t << "    {\"" << hook << "\", " << original_name(addr) << "},\n";
    if (opt.wraps.empty()) t << "    {nullptr, nullptr},\n";
    t << "};\nconst size_t original_count = " << opt.wraps.size() << ";\n\n";
    t << "const char* const imports[] = {\n";
    for (const auto& [name, i] : import_names_) t << "    \"" << name << "\",\n";
    t << "};\nconst size_t import_count = " << import_names_.size() << ";\n\n}  // namespace recomp::gen\n";
    if (!write_file(fs::path(opt.out_dir) / "table.cpp", t.str(), error)) return false;

    // File list for the build system.
    std::ostringstream l;
    for (size_t i = 0; i < file_no; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "funcs_%03zu.cpp", i);
        l << name << "\n";
    }
    l << "table.cpp\n";
    return write_file(fs::path(opt.out_dir) / "sources.txt", l.str(), error);
}

}  // namespace recomp
