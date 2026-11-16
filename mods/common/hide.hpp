// SPDX-License-Identifier: GPL-3.0-or-later
// Shared mod helper: Lua for menus built from `ui` Frames (children, name,
// setVisible, onPointerEvent), as in Classic, Seasons, Space and Star Wars. Put
// kHideLua in front of a mod's Lua (append_lua).
//
//   hide_in(frame, names)          hides the named descendants of `frame`
//   hide_buttons(Class, names)     hides them after every Class:update. They stay
//                                  in the frame: layouts read their positions,
//                                  and menus show some of them again.
//   disarm_in(frame, names)        the named descendants stay as they are but
//                                  ignore the pointer: for buttons drawn into
//                                  the scenery, which would leave a hole
//   disarm_buttons(Class, names)   disarm_in after every Class:update
//
// `names` is a set: {facebookButton = true, ...}.
#pragma once

namespace slingshot::mods {

constexpr const char* kHideLua = R"(
local function hide_in(frame, names)
    for _, child in ipairs(frame.children or {}) do
        if names[child.name] then
            if child.visible ~= false then
                child:setVisible(false)
                if child.setEnabled then child:setEnabled(false) end
            end
        else
            hide_in(child, names)
        end
    end
end
local function hide_buttons(class, names)
    local update = class.update
    function class:update(...)
        local results = {update(self, ...)}
        hide_in(self, names)
        return unpack(results)
    end
end
local function ignore_pointer() end
local function disarm_in(frame, names)
    for _, child in ipairs(frame.children or {}) do
        if names[child.name] then
            child.onPointerEvent = ignore_pointer
        else
            disarm_in(child, names)
        end
    end
end
local function disarm_buttons(class, names)
    local update = class.update
    function class:update(...)
        local results = {update(self, ...)}
        disarm_in(self, names)
        return unpack(results)
    end
end
)";

}  // namespace slingshot::mods
