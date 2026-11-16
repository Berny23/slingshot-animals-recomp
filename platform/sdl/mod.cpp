// SPDX-License-Identifier: GPL-3.0-or-later
#include "mod.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace slingshot {
namespace {

std::vector<ModInfo>& registry() {
    static std::vector<ModInfo> mods;
    return mods;
}

std::map<std::pair<std::string, std::string>, std::string>& options() {
    static std::map<std::pair<std::string, std::string>, std::string> o;
    return o;
}

}  // namespace

void set_mod_option(const std::string& mod, const std::string& key, const std::string& value) {
    options()[{mod, key}] = value;
}

const std::string* mod_option(const std::string& mod, const std::string& key) {
    const auto it = options().find({mod, key});
    return it == options().end() ? nullptr : &it->second;
}

bool register_mod(const ModInfo& info) {
    registry().push_back(info);
    return true;
}

const std::vector<ModInfo>& available_mods() { return registry(); }

bool load_mods(const std::vector<std::string>& names, std::vector<std::unique_ptr<Mod>>& out) {
    bool ok = true;
    for (const std::string& name : names) {
        const ModInfo* found = nullptr;
        for (const ModInfo& m : registry())
            if (name == m.name) found = &m;
        if (!found) {
            std::fprintf(stderr, "host: unknown mod '%s' (see --list-mods)\n", name.c_str());
            ok = false;
            continue;
        }
        std::fprintf(stderr, "host: mod %s enabled\n", found->name);
        out.push_back(found->create());
    }
    return ok;
}

}  // namespace slingshot
