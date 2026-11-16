// SPDX-License-Identifier: GPL-3.0-or-later
#include "script_link.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>

#include "recomp/vfs.hpp"

namespace slingshot::mods {

void on_script_message(const std::string& prefix, std::function<void(const std::string&)> handler) {
    recomp::vfs_add_virtual_fallback(
        [prefix, handler = std::move(handler)](const std::string& path) -> std::optional<std::string> {
            if (path.compare(0, prefix.size(), prefix)) return std::nullopt;
            std::string text;
            for (size_t i = prefix.size(); i + 1 < path.size(); i += 2)
                text += char(std::strtol(path.substr(i, 2).c_str(), nullptr, 16));
            handler(text);
            return std::string();
        });
}

}  // namespace slingshot::mods
