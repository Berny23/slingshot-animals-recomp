// SPDX-License-Identifier: GPL-3.0-or-later
// ELF32 ARM shared-object loader: gives a flat view of the image by virtual
// address plus the dynamic symbol table, relocations and the PLT import map.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace recomp {

struct Section {
    std::string name;
    uint32_t type = 0;
    uint32_t flags = 0;
    uint32_t addr = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct Segment {
    uint32_t type = 0;
    uint32_t flags = 0;
    uint32_t vaddr = 0;
    uint32_t offset = 0;
    uint32_t filesz = 0;
    uint32_t memsz = 0;
};

struct Symbol {
    std::string name;
    uint32_t value = 0;  // includes Thumb bit for Thumb functions
    uint32_t size = 0;
    uint8_t type = 0;    // STT_*
    uint8_t bind = 0;    // STB_*
    uint16_t shndx = 0;
    bool defined() const { return shndx != 0; }
};

struct Relocation {
    uint32_t offset = 0;  // vaddr patched
    uint32_t type = 0;    // R_ARM_*
    uint32_t sym = 0;     // dynsym index
};

// R_ARM_* relocation types used here.
enum : uint32_t {
    R_ARM_ABS32 = 2,
    R_ARM_GLOB_DAT = 21,
    R_ARM_JUMP_SLOT = 22,
    R_ARM_RELATIVE = 23,
};

class ElfImage {
public:
    static std::optional<ElfImage> load(const std::string& path, std::string& error);

    // Memory view (loaded image, BSS reads as zero).
    uint32_t image_end() const { return static_cast<uint32_t>(memory_.size()); }
    bool contains(uint32_t addr, uint32_t len = 1) const {
        return addr <= memory_.size() && len <= memory_.size() - addr;
    }
    uint8_t read8(uint32_t a) const { return memory_[a]; }
    uint16_t read16(uint32_t a) const { return uint16_t(memory_[a] | memory_[a + 1] << 8); }
    uint32_t read32(uint32_t a) const {
        return uint32_t(memory_[a]) | uint32_t(memory_[a + 1]) << 8 |
               uint32_t(memory_[a + 2]) << 16 | uint32_t(memory_[a + 3]) << 24;
    }
    std::span<const uint8_t> bytes(uint32_t addr, uint32_t len) const {
        return {memory_.data() + addr, len};
    }

    const std::vector<Section>& sections() const { return sections_; }
    const std::vector<Segment>& segments() const { return segments_; }
    const std::vector<Symbol>& dynsyms() const { return dynsyms_; }
    const std::vector<Relocation>& relocations() const { return relocs_; }
    const Section* section(const std::string& name) const;

    // Is addr inside an executable PT_LOAD segment?
    bool is_exec(uint32_t addr) const;
    // Range of .text (the recompiled code).
    uint32_t text_begin() const { return text_begin_; }
    uint32_t text_end() const { return text_end_; }

    // PLT stub address -> imported symbol name.
    const std::unordered_map<uint32_t, std::string>& plt_imports() const { return plt_imports_; }

private:
    void build_plt_map();

    std::vector<uint8_t> file_;
    std::vector<uint8_t> memory_;
    std::vector<Section> sections_;
    std::vector<Segment> segments_;
    std::vector<Symbol> dynsyms_;
    std::vector<Relocation> relocs_;
    std::unordered_map<uint32_t, std::string> plt_imports_;
    uint32_t text_begin_ = 0, text_end_ = 0;
};

}  // namespace recomp
