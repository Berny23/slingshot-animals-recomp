// SPDX-License-Identifier: GPL-3.0-or-later
// mouse-wheel for Star Wars (see mouse_wheel.hpp).
#include "mouse_wheel/mouse_wheel.hpp"

namespace {

constexpr const char* kAdapter = R"(
function WH.in_level() return isInGameMode ~= nil and isInGameMode() end
)";

const std::string kGameScene = slingshot::mods::mouse_wheel::camera_pages_lua(
    "not g_inMainMenu and not isInGameMode() and not (g_gestureMenuTriggered and g_gestureMenuTriggered > 0)",
    "{'EpisodeSelectionFrame'}");

}  // namespace

SLINGSHOT_MOUSE_WHEEL_MOD(kAdapter, {{"data/scripts_common/ui_components/ScrollFrame2.lua",
                                      slingshot::mods::mouse_wheel::kScrollFrameLua},
                                     {"data/scripts/gamescene.lua", kGameScene.c_str()}});
