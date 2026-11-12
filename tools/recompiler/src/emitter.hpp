// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the generated C++ sources for all recovered functions.
#pragma once

#include <map>
#include <string>

#include "analysis.hpp"
#include "lifter.hpp"

namespace recomp {

struct EmitOptions {
    std::string out_dir;
    size_t file_count = 128;  // fixed, so the build system knows the outputs up front
    std::string lib_sha256;  // recorded in the generated table, checked by the runtime
    // Functions whose callers reach a runtime hook instead (entry address -> hook
    // name); the original code stays available to the hook (recomp::find_original).
    std::map<uint32_t, std::string> wraps;
};

struct EmitStats {
    size_t functions = 0, instructions = 0, unimplemented = 0, files = 0;
    std::map<std::string, size_t> unimplemented_by_mnemonic;
};

class Emitter {
public:
    Emitter(const ElfImage& image, const Analysis& analysis) : image_(image), an_(analysis), lifter_(image) {}
    bool emit(const EmitOptions& opt, EmitStats& stats, std::string& error);

private:
    std::string emit_function(const Function& fn, EmitStats& stats, const EmitOptions& opt);
    std::string call_code(const Function& fn, uint32_t target, Mode mode, bool tail) const;
    std::string fallthrough_code(const Function& fn, uint32_t next) const;
    static std::string fn_name(uint32_t addr);
    static std::string original_name(uint32_t addr);

    const ElfImage& image_;
    const Analysis& an_;
    Lifter lifter_;
    bool in_try_ = false;  // emitting a function with resume points (no musttail inside try)
    std::map<uint32_t, unsigned> import_index_;  // PLT stub -> import table index
    std::map<std::string, unsigned> import_names_;
};

}  // namespace recomp
