// SPDX-License-Identifier: GPL-3.0-or-later
// System fonts for SystemFontRenderer, found with fontconfig.
#pragma once

#include "recomp/java.hpp"

namespace slingshot {

class FontconfigProvider final : public recomp::java::FontProvider {
public:
    std::optional<recomp::java::FontFile> typeface(const recomp::java::FontRequest& request) override;
    std::optional<recomp::java::FontFile> fallback(const recomp::java::FontRequest& request, char32_t cp) override;
};

}  // namespace slingshot
