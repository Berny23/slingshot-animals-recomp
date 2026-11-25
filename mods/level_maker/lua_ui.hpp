// SPDX-License-Identifier: GPL-3.0-or-later
// level-maker: a small immediate-mode UI drawn with the engine's primitives.
//
// A screen is a ui.Frame whose draw() calls a builder each frame; the builder draws
// and registers hit areas (UI.button and friends). Pointer events go to the area
// under the finger: tap, long press (or right click, or the gamepad's X) and drag.
// Sizes are in units of a 720 pixel high screen.
#pragma once

namespace slingshot::level_maker {

constexpr const char* kUiLua = R"(
local G = _G
local LM = G.levelMaker
local UI = LM.UI or {}
LM.UI = UI

function UI.k() return (screenHeight or 720) / 720 end
function UI.language() return tostring(res.getLocale()) end
UI.text = statistics.texts(LM.TEXTS, UI.language)

-- Russian, Japanese and Chinese have fonts of their own; FONT_HEADER has only
-- English and German letters.
function UI.font(font)
    local t = {}
    local reload = reloadLocaleFonts or (gamelua and gamelua.reloadLocaleFonts)
    if reload then pcall(reload, t) end
    if t.font and t.font ~= 'FONT_BASIC' then return t.font end
    local l = string.sub(UI.language(), 1, 2)
    if font == 'FONT_HEADER' and l ~= 'en' and l ~= 'de' then return 'FONT_BASIC' end
    return font or 'FONT_BASIC'
end

local leading = {}
-- Text with its height (720 units) at x, y; anchors as res.drawString.
-- A font name starting with '=' is used as it is, in every language (for signs such as '90°').
function UI.label(s, x, y, size, font, ha, va, maxw)
    local k = UI.k()
    if font and string.sub(font, 1, 1) == '=' then font = string.sub(font, 2) else font = UI.font(font) end
    setFont(font)
    leading[font] = leading[font] or math.max(res.getFontLeading() or 30, 1)
    local scale = (size or 30) * k / leading[font]
    if maxw then
        local w = (res.getStringWidth(s) or 0) * scale
        if w > maxw * k and w > 0 then scale = scale * maxw * k / w end
    end
    setRenderState(0, 0, scale, scale, 0, 0, 0, 1)
    res.drawString('', s, x * k / scale, y * k / scale, va or 'VCENTER', ha or 'HCENTER')
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
end

function UI.box(x, y, w, h, c, a)
    local k = UI.k()
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    drawRect(c[1], c[2], c[3], a or c[4] or 1, x * k, y * k, (x + w) * k, (y + h) * k, true)
end

-- A box with round corners (radius r), drawn a screen pixel row at a time, the
-- rows' ends partly covered for smooth edges.
function UI.round(x, y, w, h, c, a, r)
    local k = UI.k()
    local X, Y, W, Hh = math.floor(x * k + 0.5), math.floor(y * k + 0.5), math.floor(w * k + 0.5), math.floor(h * k + 0.5)
    local R = math.floor(math.min((r or 14) * k, W * 0.5, Hh * 0.5))
    local cr, cg, cb, alpha = c[1], c[2], c[3], a or c[4] or 1
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    drawRect(cr, cg, cb, alpha, X, Y + R, X + W, Y + Hh - R, true)
    for i = 0, R - 1 do
        local dy = R - i - 0.5
        local e = R - math.sqrt(math.max(R * R - dy * dy, 0))
        local full = math.ceil(e)
        local part = full - e
        for _, row in ipairs({Y + i, Y + Hh - 1 - i}) do
            drawRect(cr, cg, cb, alpha, X + full, row, X + W - full, row + 1, true)
            if part > 0.05 and full > 0 then
                drawRect(cr, cg, cb, alpha * part, X + full - 1, row, X + full, row + 1, true)
                drawRect(cr, cg, cb, alpha * part, X + W - full, row, X + W - full + 1, row + 1, true)
            end
        end
    end
end

-- ---- the game's own boxes and buttons, drawn in pieces
-- One piece: sprite `name` scaled sx, sy around its pivot at screen pixel x, y;
-- tint multiplies its colours ('grey' for the game's grey shader).
local function piece(name, x, y, sx, sy, tint, alpha)
    alpha = alpha or 1
    setRenderState(0, 0, sx, sy, 0, 0, 0, alpha)
    if tint == 'grey' then drawSpriteWithShader(name, x, y, '2d-greycolor', 1, 1, 1, alpha)
    elseif tint then drawSpriteWithShader(name, x, y, '2d-sprite-fill', tint[1], tint[2], tint[3], alpha)
    else res.drawSprite('', name, x / sx, y / sy) end
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
end
local function loaded(name)
    local w = res.getSpriteBounds('', name)
    return w and w > 0
end

-- Boxes of nine pieces (corners at the game's size, edges and middle stretched).
UI.BOXES = {
    popup = 'LEAGUE_MODERN_BOX_',  -- Mighty League's box, tinted dark grey (UI.POPUP_TINT)
    orange = 'POPUP_',             -- the orange popup
    dark = 'POPUP_BLACK_',         -- dark, see-through
    paper = 'TUTORIAL_',           -- white with a gold edge (the tutorials'); takes any colour as tint
}
local NINE = {'TOP_LEFT', 'TOP_MIDDLE', 'TOP_RIGHT', 'LEFT', 'CENTER', 'RIGHT', 'BOTTOM_LEFT', 'BOTTOM_MIDDLE', 'BOTTOM_RIGHT'}
UI.POPUP_TINT = {0.4, 0.23, 0.195}
local NINE_LEAGUE = {'LEFT_TOP', 'CENTER_TOP', 'RIGHT_TOP', 'LEFT_MIDDLE', 'CENTER_MIDDLE', 'RIGHT_MIDDLE', 'LEFT_BOTTOM', 'CENTER_BOTTOM', 'RIGHT_BOTTOM'}
function UI.panel(x, y, w, h, style, tint, alpha)
    if (style or 'popup') == 'popup' and not tint then tint = UI.POPUP_TINT end
    local p = UI.BOXES[style or 'popup'] or style
    local names = p == UI.BOXES.popup and NINE_LEAGUE or NINE
    if not loaded(p .. names[1]) or not loaded(p .. names[9]) then
        return UI.round(x, y, w, h, UI.COLORS.panel, alpha, 24)
    end
    local k = UI.k()
    local X, Y, W, Hh = math.floor(x * k + 0.5), math.floor(y * k + 0.5), math.floor(w * k + 0.5), math.floor(h * k + 0.5)
    local n = Scaling and Scaling.getScaleNum and Scaling.getScaleNum() or k
    local cw, ch = res.getSpriteBounds('', p .. names[1])
    local bw, bh = res.getSpriteBounds('', p .. names[9])
    -- small boxes: smaller corners
    n = math.min(n, W / (cw + bw), Hh / (ch + bh))
    local x0, y0 = math.floor(X + cw * n + 0.5), math.floor(Y + ch * n + 0.5)
    local x1, y1 = math.floor(X + W - bw * n + 0.5), math.floor(Y + Hh - bh * n + 0.5)
    local iw, ih = math.max(x1 - x0, 0), math.max(y1 - y0, 0)
    local function at(name, px, py, sx, sy)
        local pw, ph = res.getSpriteBounds('', name)
        local ox, oy = res.getSpritePivot('', name)
        -- stretched pieces: their pivot moves with the stretch; a pixel more on each side
        -- covers the faded edges of the pieces next to them
        local fx = sx == 'w' and (iw + 2) / pw or n
        local fy = sy == 'h' and (ih + 2) / ph or n
        if sx == 'w' then px = px - 1 + ox * fx end
        if sy == 'h' then py = py - 1 + oy * fy end
        if fx > 0 and fy > 0 then piece(name, px, py, fx, fy, tint, alpha) end
    end
    at(p .. names[1], x0, y0) at(p .. names[2], x0, y0, 'w') at(p .. names[3], x1, y0)
    at(p .. names[4], x0, y0, nil, 'h') at(p .. names[5], x0, y0, 'w', 'h') at(p .. names[6], x1, y0, nil, 'h')
    at(p .. names[7], x0, y1) at(p .. names[8], x0, y1, 'w') at(p .. names[9], x1, y1)
end

-- The game's glossy button (left, middle, right), as tall as the button.
local PILL = 'PLAY_BUTTON_BG_'
function UI.pill(x, y, w, h, tint, alpha)
    if not loaded(PILL .. 'LEFT') then return UI.round(x, y, w, h, UI.COLORS.button, alpha, 16) end
    local k = UI.k()
    local X, Y, W, Hh = math.floor(x * k + 0.5), math.floor(y * k + 0.5), math.floor(w * k + 0.5), math.floor(h * k + 0.5)
    local lw, lh = res.getSpriteBounds('', PILL .. 'LEFT')
    local rw = res.getSpriteBounds('', PILL .. 'RIGHT')
    local mw = res.getSpriteBounds('', PILL .. 'MIDDLE')
    local n = Hh / lh
    local nx = math.min(n, W / (lw + rw))
    local lpx, lpy = res.getSpritePivot('', PILL .. 'LEFT')
    local mpx = res.getSpritePivot('', PILL .. 'MIDDLE')
    local x0, x1 = math.floor(X + lw * nx + 0.5), math.floor(X + W - rw * nx + 0.5)
    local cy = Y + lpy * n
    piece(PILL .. 'LEFT', x0, cy, nx, n, tint, alpha)
    if x1 > x0 then
        local sx = (x1 - x0 + 2) / mw
        piece(PILL .. 'MIDDLE', x0 - 1 + mpx * sx, cy, sx, n, tint, alpha)
    end
    piece(PILL .. 'RIGHT', x1, cy, nx, n, tint, alpha)
end

-- A sprite fitted into w x h around x, y (mirrored: flipped left to right);
-- false when its sheet is not loaded.
function UI.sprite(name, x, y, w, h, alpha, mirrored, grey)
    local sw, sh = res.getSpriteBounds('', name)
    if not sw or sw <= 0 or not sh or sh <= 0 then return false end
    local k = UI.k()
    local s = math.min(w * k / sw, h * k / sh)
    local sx = mirrored and -s or s
    setRenderState(0, 0, sx, s, 0, 0, 0, alpha or 1)
    if grey then
        -- a grey silhouette, the sprite faintly over it
        drawSpriteWithShader(name, x * k, y * k, '2d-greycolor', 0.45, 0.45, 0.45, alpha or 1)
        setRenderState(0, 0, sx, s, 0, 0, 0, (alpha or 1) * 0.3)
    end
    res.drawSprite('', name, x * k / sx, y * k / s)
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    return true
end

-- A sprite at the game's own size, smaller where it does not fit w x h; sprites
-- smaller than `least` (720 units) on both sides are scaled up to it, as far as they fit.
function UI.icon(name, x, y, w, h, least)
    local sw, sh = res.getSpriteBounds('', name)
    if not sw or sw <= 0 or not sh or sh <= 0 then return false end
    local k = UI.k()
    local native = (Scaling and Scaling.getScaleNum and Scaling.getScaleNum() or k) / k
    local fit = math.min(w / sw, h / sh)
    local s = math.min(native, fit)
    least = least or 28
    if math.max(sw, sh) * s < least then s = math.min(fit, least / math.max(sw, sh)) end
    return UI.sprite(name, x, y, sw * s, sh * s)
end

UI.COLORS = {
    panel = {0.08, 0.16, 0.3, 0.88}, button = {0.16, 0.42, 0.75, 1}, play = {0.25, 0.62, 0.2, 1},
    danger = {0.75, 0.2, 0.15, 1}, off = {0.35, 0.38, 0.42, 1}, on = {0.95, 0.65, 0.1, 1}, dim = {0, 0, 0, 0.6},
    light = {1, 1, 1, 0.18},
}
-- Tints of the game's button for these colours: on the glossy (yellow) button, and on a
-- tall button (the white tutorial box).
local PILL_TINTS = {play = {0.45, 1, 0.4}, danger = {1, 0.45, 0.38}, off = {0.72, 0.72, 0.72}}
local CARD_TINTS = {button = {0.5, 0.72, 1}, play = {0.55, 0.9, 0.4}, danger = {1, 0.5, 0.45}, on = {1, 0.8, 0.3}, off = {0.72, 0.74, 0.78}}
local function colour_name(c)
    for k, v in pairs(UI.COLORS) do if v == c then return k end end
end

-- ---- hit areas
UI.areas = {}
function UI.area(x, y, w, h, handlers)
    handlers.x, handlers.y, handlers.w, handlers.h = x, y, w, h
    UI.areas[#UI.areas + 1] = handlers
    return handlers
end

-- A button: text and/or a sprite; opts = {color, size, icon, glyph, tip, tap, long, enabled, selected, grey (a grey icon, still tappable), id}.
-- glyph: a short sign on a round game button; tip: shown on a long press. An icon or glyph
-- alone, without a colour, is drawn without a box: the game's icons are buttons already.
function UI.button(x, y, w, h, label, opts)
    opts = opts or {}
    if opts.tip and not opts.long then
        local tip = opts.tip
        opts.long = function() UI.tip(tip, x + w * 0.5, y + h) end
    end
    local enabled = opts.enabled ~= false
    local c = enabled and (opts.color or UI.COLORS.button) or UI.COLORS.off
    local pressed = UI.pressed and UI.pressed.id ~= nil and UI.pressed.id == (opts.id or label)
    local bare = (opts.icon or opts.glyph) and not label and not opts.color and not opts.selected
    if not bare then
        local name = colour_name(c) or 'button'
        if opts.selected == false and enabled then name = 'off' end
        if opts.selected then name = 'on' end
        -- pressed: darker (see-through pieces would show where they overlap)
        local function shade(t)
            if not pressed then return t end
            t = t or {1, 1, 1}
            return {t[1] * 0.75, t[2] * 0.75, t[3] * 0.75}
        end
        if h > 100 then UI.panel(x, y, w, h, 'paper', shade(CARD_TINTS[name] or CARD_TINTS.button))
        else UI.pill(x, y, w, h, shade(PILL_TINTS[name])) end
    end
    local ax, ay, aw, ah = x, y, w, h
    local alpha = enabled and 1 or 0.4
    if bare and pressed then x, y, w, h = x + w * 0.05, y + h * 0.05, w * 0.9, h * 0.9 end
    local tx, ty = x + w * 0.5, y + h * 0.5
    if opts.glyph then
        -- on a round game button
        UI.sprite('BUTTON_EMPTY', x + w * 0.5, y + h * 0.5, math.min(w, h), math.min(w, h), alpha)
        UI.label(opts.glyph, x + w * 0.5, y + h * 0.5, math.min(w, h) * 0.45, '=FONT_BASIC')
    end
    local maxw = w - 16
    if opts.icon and label and h >= 200 then
        -- tall: the icon above the text
        local iw = math.min(w, h) * 0.4
        UI.sprite(opts.icon, x + w * 0.5, y + h * 0.42, iw, iw, alpha, opts.mirrored)
        ty = y + h * 0.42 + iw * 0.5 + 40
    elseif opts.icon and label then
        local iw = math.min(h - 14, w * 0.4)
        UI.sprite(opts.icon, x + 10 + iw * 0.5, y + h * 0.5, iw, iw, alpha, opts.mirrored)
        tx = x + 16 + iw + (w - iw - 16) * 0.5
        maxw = w - iw - 30
    elseif opts.icon then
        local iw = math.min(w, h) - (bare and 0 or 14)
        UI.sprite(opts.icon, x + w * 0.5, y + h * 0.5, iw, iw, alpha, opts.mirrored, opts.grey)
    end
    if label then
        UI.label(label, tx, ty, opts.size or math.min(32, h * 0.45), opts.font or 'FONT_BASIC', 'HCENTER', 'VCENTER', math.max(maxw, 10))
    end
    if enabled then
        UI.area(ax, ay, aw, ah, {id = opts.id or label, tap = opts.tap, long = opts.long, drag = opts.drag})
    end
end

-- ---- the screen frame
UI.Screen = UI.Screen or G.lua.Class.create(ui.Frame)
function UI.Screen:init(params)
    ui.Frame.init(self, params)
    self.build = params.build
end
function UI.Screen:draw()
    UI.areas = {}
    local ok, err = pcall(self.build, self)
    if not ok and G.native and G.native.logError then G.native.logError('level-maker: ' .. tostring(err)) end
    local t = UI.tipShown
    if t then
        t.frames = t.frames - 1
        if t.frames <= 0 then UI.tipShown = nil end
        local k = UI.k()
        setFont(UI.font('FONT_BASIC'))
        local tw = math.min(600, (res.getStringWidth(t.text) or 200) * 26 / math.max(res.getFontLeading() or 30, 1) / 1 + 30)
        local tx = math.max(10, math.min((screenWidth / k) - tw - 10, t.x - tw * 0.5))
        local ty = t.y + 8
        if ty + 44 > 720 then ty = t.y - 120 end
        UI.panel(tx, ty, tw, 44, 'popup')
        UI.label(t.text, tx + tw * 0.5, ty + 22, 26, 'FONT_BASIC', 'HCENTER', 'VCENTER', tw - 20)
    end
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
end
local function hit(x, y)
    local k = UI.k()
    for i = #UI.areas, 1, -1 do
        local a = UI.areas[i]
        if x >= a.x * k and x <= (a.x + a.w) * k and y >= a.y * k and y <= (a.y + a.h) * k then return a end
    end
end
UI.hit = hit
local LONG = 30
function UI.Screen:onPointerEvent(kind, x, y)
    if kind == 'LPRESS' then
        local a = hit(x, y)
        UI.pressed = a
        UI.pressX, UI.pressY, UI.held, UI.moved, UI.longDone = x, y, 0, false, false
        if a and a.drag then a.drag('start', x, y) end
    elseif kind == 'LHOLD' and UI.pressed then
        local a = UI.pressed
        if math.abs(x - UI.pressX) + math.abs(y - UI.pressY) > 12 * UI.k() then UI.moved = true end
        if a.drag then a.drag('move', x, y) end
        UI.held = UI.held + 1
        if a.long and not UI.moved and not UI.longDone and UI.held > LONG then
            UI.longDone = true
            a.long(x, y)
        end
    elseif kind == 'LRELEASE' then
        local a = UI.pressed
        UI.pressed = nil
        if a then
            if a.drag then a.drag('end', x, y) end
            local b = hit(x, y)
            if not UI.longDone and (not UI.moved or not a.drag) and b and b.id == a.id and a.tap then a.tap(x, y) end
        end
    elseif kind == 'WHEEL' and UI.onWheel then
        UI.onWheel((gamelua.cursor or {}).wheel or 0, x, y)
    end
    return 'BLOCK'
end
function UI.Screen:onKeyEvent(kind, key)
    if kind == 'RELEASE' and (key == 'BACK' or key == 'ESCAPE') and UI.onBack then UI.onBack() end
    return 'BLOCK'
end
function UI.tip(text, x, y) UI.tipShown = {text = text, x = x, y = y, frames = 120} end

-- Right click and the gamepad's X: the long press of the area there.
function UI.secondary(x, y)
    local a = hit(x, y)
    if a and a.long then a.long(x, y) return true end
    return false
end
)";

}  // namespace slingshot::level_maker
