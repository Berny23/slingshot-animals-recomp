// SPDX-License-Identifier: GPL-3.0-or-later
// abrecomp: static recompiler for the ARMv7 game library.
//
//   abrecomp report <lib.so>             analysis statistics
//   abrecomp disasm <lib.so> <addr>      dump one recovered function
//   abrecomp emit   <lib.so> <out_dir>   generate C++ sources
//   abrecomp match  <lib.so> <addr>... -- <other.so>
//                                        functions of other.so most similar to the given ones
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "analysis.hpp"
#include "disasm.hpp"
#include "elf_image.hpp"
#include "emitter.hpp"

using namespace recomp;

namespace {

int cmd_report(const ElfImage& img, Analysis& an, Disassembler& dis) {
    size_t arm = 0, thumb = 0, stubs = 0, insns = 0, noret = 0;
    std::map<Flow, size_t> flows;
    std::map<std::string, size_t> warnings, mnemonics;
    for (const auto& [e, fn] : an.functions()) {
        if (an.is_import_stub(e)) {
            ++stubs;
            continue;
        }
        (fn.mode == Mode::Thumb ? thumb : arm)++;
        noret += fn.noreturn;
        insns += fn.insns.size();
        for (const auto& [a, info] : fn.insns) {
            flows[info.flow]++;
            if (info.insn) mnemonics[std::string(fn.mode == Mode::Thumb ? "T:" : "A:") + info.insn->name]++;
        }
        for (const auto& w : fn.warnings) warnings[w]++;
    }
    std::printf("functions: %zu ARM, %zu Thumb, %zu import stubs, %zu noreturn\n", arm, thumb, stubs, noret);
    std::printf("instructions: %zu\n", insns);
    std::map<std::string, std::pair<size_t, size_t>> origins;
    for (const auto& [e, fn] : an.functions()) (fn.mode == Mode::Thumb ? origins[fn.origin].second : origins[fn.origin].first)++;
    for (auto& [o, n] : origins) std::printf("  origin %-12s arm %zu thumb %zu\n", o.c_str(), n.first, n.second);
    for (auto [f, n] : flows) std::printf("  flow %-14s %zu\n", flow_name(f), n);
    for (auto& [w, n] : warnings) std::printf("  warning: %-30s %zu functions\n", w.c_str(), n);

    // Coverage of .text by recovered instructions (by bytes).
    size_t covered = 0;
    for (const auto& [e, fn] : an.functions())
        for (const auto& [a, info] : fn.insns) covered += info.insn ? info.insn->size : 0;
    std::printf("text: %u bytes, decoded (incl. overlaps) %zu bytes\n", img.text_end() - img.text_begin(), covered);

    // Samples of problematic sites, for investigation.
    std::map<std::string, std::vector<uint32_t>> samples;
    for (const auto& [e, fn] : an.functions()) {
        for (const auto& w : fn.warnings)
            if (samples[w].size() < 12) samples[w].push_back(e);
        for (const auto& [a, info] : fn.insns)
            if (info.flow == Flow::IndirectJump || info.flow == Flow::Invalid) {
                std::string k = std::string(flow_name(info.flow)) + ": " +
                                (info.insn ? info.insn->mnemonic + " " + info.insn->op_str : "?");
                if (samples[k].size() < 4) samples[k].push_back(a);
            }
    }
    std::multimap<size_t, std::string, std::greater<>> sorted;
    for (auto& [k, v] : samples) sorted.emplace(v.size(), k);
    for (auto& [n, k] : sorted) {
        std::printf("  [%s]", k.c_str());
        for (uint32_t a : samples[k]) std::printf(" %x", a);
        std::printf("\n");
    }
    // Mixed-mode exidx regions are suspicious (compiler output is single-mode per function).
    std::map<uint32_t, std::pair<int, int>> region_modes;  // begin -> (arm, thumb)
    for (const auto& [e, fn] : an.functions()) {
        auto& rm = region_modes[fn.range_begin];
        (fn.mode == Mode::Thumb ? rm.second : rm.first)++;
    }
    size_t mixed = 0;
    for (auto& [b, rm] : region_modes)
        if (rm.first && rm.second) {
            if (mixed < 15) {
                std::printf("  mixed region %x:", b);
                for (const auto& [e, fn] : an.functions())
                    if (fn.range_begin == b)
                        std::printf(" %x%s(%s)", e, fn.mode == Mode::Thumb ? "T" : "A", fn.origin);
                std::printf("\n");
            }
            ++mixed;
        }
    size_t ok = 0, bad = 0;
    for (const auto& sym : img.dynsyms())
        if (sym.defined() && sym.type == 2 && img.is_exec(sym.value & ~1u) && (sym.value & ~1u) >= img.text_begin()) {
            Mode want = (sym.value & 1) ? Mode::Thumb : Mode::Arm;
            if (an.guess_mode(sym.value & ~1u) == want) ++ok;
            else { if (bad < 5) std::printf("  mode guess wrong at %x (%s)\n", sym.value & ~1u, sym.name.c_str()); ++bad; }
        }
    std::printf("mode heuristic vs symbols: %zu ok, %zu wrong\n", ok, bad);
    std::printf("mixed-mode regions: %zu\n", mixed);
    std::printf("exidx entries: %zu\n", an.exidx().size());
    std::printf("distinct mnemonics: %zu\n", mnemonics.size());
    std::multimap<size_t, std::string, std::greater<>> by_count;
    for (auto& [m, n] : mnemonics) by_count.emplace(n, m);
    for (auto& [n, m] : by_count) std::printf("  %-14s %zu\n", m.c_str(), n);
    (void)dis;
    return 0;
}

int cmd_disasm(Analysis& an, uint32_t addr) {
    auto it = an.functions().find(addr & ~1u);
    if (it == an.functions().end()) {
        std::fprintf(stderr, "no function at 0x%x\n", addr);
        return 1;
    }
    const Function& fn = it->second;
    std::printf("function 0x%x %s mode=%s range=[0x%x,0x%x) noreturn=%d origin=%s\n", fn.entry, an.name_of(fn.entry).c_str(),
                fn.mode == Mode::Thumb ? "thumb" : "arm", fn.range_begin, fn.range_end, fn.noreturn, fn.origin);
    for (const auto& w : fn.warnings) std::printf("  ! %s\n", w.c_str());
    for (const auto& [a, info] : fn.insns) {
        if (fn.labels.count(a)) std::printf("L_%x:\n", a);
        std::printf("  %08x  %-40s ; %s", a, info.insn ? info.insn->text().c_str() : "<invalid>", flow_name(info.flow));
        if (info.flow == Flow::Call || info.flow == Flow::TailCall) {
            std::string n = an.name_of(info.target);
            std::printf(" -> 0x%x %s", info.target, n.c_str());
        }
        std::printf("\n");
        if (auto jt = fn.jump_tables.find(a); jt != fn.jump_tables.end()) {
            std::printf("            table:");
            for (uint32_t t : jt->second.targets) std::printf(" %x", t);
            std::printf("\n");
        }
    }
    return 0;
}

int cmd_raw(Disassembler& dis, uint32_t addr, Mode mode, int count) {
    for (int i = 0; i < count; ++i) {
        const Insn* in = dis.decode(addr, mode);
        if (!in) {
            std::printf("  %08x  <invalid>\n", addr);
            addr += mode == Mode::Thumb ? 2 : 4;
            continue;
        }
        std::printf("  %08x  %08x  %s\n", addr, in->raw, in->text().c_str());
        addr += in->size;
    }
    return 0;
}

int cmd_xref(Analysis& an, uint32_t target) {
    for (const auto& [e, fn] : an.functions())
        for (const auto& [a, info] : fn.insns)
            if ((info.flow == Flow::Call || info.flow == Flow::TailCall || info.flow == Flow::Branch) &&
                info.target == target)
                std::printf("%x in fn %x (%s, origin=%s): %s\n", a, e, fn.mode == Mode::Thumb ? "thumb" : "arm",
                            fn.origin, info.insn->text().c_str());
    return 0;
}

// One token per instruction: its name, and for calls the callee's symbol name.
std::vector<std::string> tokens(const Analysis& an, const Function& fn) {
    std::vector<std::string> t;
    for (const auto& [a, info] : fn.insns) {
        std::string k = info.insn ? info.insn->name : "?";
        if (info.flow == Flow::Call || info.flow == Flow::TailCall) k += ":" + an.name_of(info.target);
        t.push_back(std::move(k));
    }
    return t;
}

// Longest common subsequence of the token sequences, relative to the longer one.
double similarity(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0;
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (const auto& x : a) {
        for (size_t j = 0; j < b.size(); ++j) cur[j + 1] = x == b[j] ? prev[j] + 1 : std::max(prev[j + 1], cur[j]);
        std::swap(prev, cur);
    }
    return double(prev[b.size()]) / double(std::max(a.size(), b.size()));
}

int cmd_match(Analysis& an, const std::vector<uint32_t>& addrs, const char* other_path) {
    std::string err;
    auto other = ElfImage::load(other_path, err);
    if (!other) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Disassembler other_dis(*other);
    Analysis other_an(*other, other_dis);
    other_an.run();
    for (uint32_t addr : addrs) {
        auto it = an.functions().find(addr & ~1u);
        if (it == an.functions().end()) {
            std::fprintf(stderr, "no function at 0x%x\n", addr);
            continue;
        }
        const auto want = tokens(an, it->second);
        std::multimap<double, uint32_t, std::greater<>> best;
        for (const auto& [e, fn] : other_an.functions()) {
            const size_t n = fn.insns.size();
            if (n * 10 < want.size() * 6 || n * 6 > want.size() * 10) continue;
            best.emplace(similarity(want, tokens(other_an, fn)), e | (fn.mode == Mode::Thumb ? 1u : 0u));
            if (best.size() > 3) best.erase(std::prev(best.end()));
        }
        std::printf("0x%x (%zu insns):", addr, want.size());
        for (auto& [score, e] : best) std::printf(" 0x%x %.2f", e, score);
        std::printf("\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s report|disasm|emit|match <lib.so> [args]\n", argv[0]);
        return 2;
    }
    const std::string cmd = argv[1];
    std::string err;
    auto img = ElfImage::load(argv[2], err);
    if (!img) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    Disassembler dis(*img);
    if (cmd == "raw" && argc >= 5)
        return cmd_raw(dis, uint32_t(std::strtoul(argv[3], nullptr, 0)),
                       std::string(argv[4]) == "thumb" ? Mode::Thumb : Mode::Arm, argc >= 6 ? std::atoi(argv[5]) : 16);
    Analysis an(*img, dis);
    an.run();
    if (cmd == "report") return cmd_report(*img, an, dis);
    if (cmd == "disasm" && argc >= 4) return cmd_disasm(an, uint32_t(std::strtoul(argv[3], nullptr, 0)));
    if (cmd == "raw" && argc >= 5) {
        // raw needs no analysis, but keep it simple
        return cmd_raw(dis, uint32_t(std::strtoul(argv[3], nullptr, 0)),
                       std::string(argv[4]) == "thumb" ? Mode::Thumb : Mode::Arm, argc >= 6 ? std::atoi(argv[5]) : 16);
    }
    if (cmd == "emit" && argc >= 4) {
        EmitOptions opt;
        opt.out_dir = argv[3];
        for (int i = 4; i + 1 < argc; i += 2) {
            const std::string a = argv[i], v = argv[i + 1];
            if (a == "--sha256") {
                opt.lib_sha256 = v;
            } else if (a == "--wrap") {  // --wrap <entry address>=<hook name>
                const size_t eq = v.find('=');
                if (eq == std::string::npos) {
                    std::fprintf(stderr, "error: --wrap wants <address>=<hook>\n");
                    return 1;
                }
                opt.wraps[uint32_t(std::strtoul(v.substr(0, eq).c_str(), nullptr, 0))] = v.substr(eq + 1);
            }
        }
        Emitter em(*img, an);
        EmitStats st;
        if (!em.emit(opt, st, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("emitted %zu functions, %zu instructions in %zu files; %zu unimplemented instructions\n",
                    st.functions, st.instructions, st.files, st.unimplemented);
        std::multimap<size_t, std::string, std::greater<>> top;
        for (auto& [m, n] : st.unimplemented_by_mnemonic) top.emplace(n, m);
        for (auto& [n, m] : top) std::printf("  unimplemented %-12s %zu\n", m.c_str(), n);
        return 0;
    }
    if (cmd == "match" && argc >= 5) {
        std::vector<uint32_t> addrs;
        int i = 3;
        for (; i < argc && std::strcmp(argv[i], "--"); ++i) addrs.push_back(uint32_t(std::strtoul(argv[i], nullptr, 0)));
        if (i + 1 < argc) return cmd_match(an, addrs, argv[i + 1]);
    }
    if (cmd == "xref" && argc >= 4) return cmd_xref(an, uint32_t(std::strtoul(argv[3], nullptr, 0)));
    std::fprintf(stderr, "unknown command\n");
    return 2;
}
