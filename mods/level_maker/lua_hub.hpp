// SPDX-License-Identifier: GPL-3.0-or-later
// level-maker: the menu behind the main menu's button. Load (a code, a QR picture,
// a saved level) on the left, build (a new or a saved level) on the right; a level
// has Play, Save, Edit, Share and Delete. Sharing needs the level beaten as it is.
#pragma once

namespace slingshot::level_maker {

constexpr const char* kHubLua = R"(
local G = _G
local LM = G.levelMaker
local UI = LM.UI
local T = UI.text
local C = UI.COLORS
local Hub = LM.Hub or {}
LM.Hub = Hub

local function W() return (screenWidth or 1280) / UI.k() end
local H = 720

function LM.newLevel()
    return {theme = 'theme1', gravity = 20, limits = {1, 1, 1, 1, 1, 1},
            objects = {{def = 'Ground', x = 25.7443, y = 5, angle = 0, z = 0, extra = {}},
                       {def = 'RedBird', x = -12.25, y = -0.97, angle = 0, z = 4, extra = {}}},
            joints = {}}
end

-- ---- pages
function Hub.push(page) Hub.stack[#Hub.stack + 1] = page Hub.page = page end
function Hub.pop()
    Hub.stack[#Hub.stack] = nil
    Hub.page = Hub.stack[#Hub.stack]
    if not Hub.page then Hub.close() end
end
function Hub.message(text, after) Hub.push({kind = 'message', text = text, after = after}) end
function Hub.confirm(text, yes) Hub.push({kind = 'confirm', text = text, yes = yes}) end

local function stats(L)
    local pigs, birds = 0, 0
    for _, o in ipairs(L.objects) do
        if LM.isPig(o.def) then pigs = pigs + 1 elseif LM.isBird(o.def) then birds = birds + 1 end
    end
    return pigs, birds
end

-- A level page: {slot = n} for a saved level, {level, code} for a loaded one.
function Hub.openLevel(slot, L, code)
    if slot then
        local s = LM.store().slots[slot]
        local level, err = LM.decode(s.code)
        if not level then return Hub.message(T('damaged') .. ' (' .. tostring(err) .. ')') end
        L, code = level, s.code
    end
    Hub.push({kind = 'level', slot = slot, level = L, code = code})
end

function Hub.load(code)
    code = string.gsub(code or '', '%s', '')
    if code == '' then return Hub.message(T('no_code')) end
    local L, err = LM.decode(code)
    if not L then return Hub.message(T('bad_code') .. (err and (' (' .. tostring(err) .. ')') or '')) end
    local slots = LM.store().slots
    for i, s in ipairs(slots) do
        if s.code == code then return Hub.openLevel(i) end
    end
    Hub.openLevel(nil, L, code)
end

function Hub.save(page)
    local slots = LM.store().slots
    if #slots >= LM.MAX_SLOTS then return Hub.message(T('slots_full')) end
    slots[#slots + 1] = {code = page.code, best = 0, stars = 0}
    LM.saveStore()
    page.slot = #slots
end

function Hub.play(page)
    LM.slot = page.slot
    LM.code = page.code
    Hub.close()
    LM.start(page.level, 'play')
end

function Hub.edit(page)
    LM.slot = page.slot
    LM.code = page.code
    LM.editorBeaten = page.slot and LM.store().slots[page.slot].beaten == page.code and page.code or nil
    Hub.close()
    LM.start(page.level, 'edit')
end

function Hub.newLevel()
    LM.slot, LM.code, LM.editorBeaten = nil, nil, nil
    Hub.close()
    LM.start(LM.newLevel(), 'edit')
end

-- Waits for an answer of the host (file dialogs).
function Hub.wait(cmd, done)
    Hub.push({kind = 'wait', cmd = cmd, done = done})
end

-- ---- drawing
local function header(title, back)
    local w = W()
    UI.box(0, 0, w, H, C.dim)
    UI.panel(40, 30, w - 80, H - 60, 'popup')
    UI.label(title, w * 0.5, 80, 46, 'FONT_HEADER')
    UI.button(w - 120, 46, 64, 64, nil, {id = 'close', icon = 'MENU_NO', tap = back or Hub.pop})
end

local function stars_row(n, x, y, size)
    for i = 1, 3 do
        if not UI.sprite(i <= (n or 0) and 'BEST_STAR_YELLOW' or 'BEST_STAR_GREY', x + (i - 2) * size * 1.1, y, size, size) then
            UI.label(i <= (n or 0) and '*' or '-', x + (i - 2) * size * 1.1, y, size)
        end
    end
end

local draw = {}

function draw.home()
    local w = W()
    header(T('title'), Hub.close)
    local bw = (w - 200) * 0.5
    UI.button(80, 150, bw, H - 260, T('load'), {id = 'load', size = 52, icon = 'GESTURE_PLAY', color = C.play, tap = function() Hub.push({kind = 'list', mode = 'load', page = 1}) end})
    UI.button(w - 80 - bw, 150, bw, H - 260, T('build'), {id = 'build', size = 52, icon = 'BUTTON_EMBLEM_EDIT', tap = function() Hub.push({kind = 'list', mode = 'build', page = 1}) end})
    UI.label(T('load_hint'), 80 + bw * 0.5, H - 140, 24, 'FONT_BASIC', 'HCENTER', 'VCENTER', bw - 20)
    UI.label(T('build_hint'), w - 80 - bw * 0.5, H - 140, 24, 'FONT_BASIC', 'HCENTER', 'VCENTER', bw - 20)
end

local PER_PAGE = 8
function draw.list(page)
    local w = W()
    header(page.mode == 'load' and T('load') or T('build'))
    local y = 130
    if page.mode == 'load' then
        local bw = (w - 200) * 0.5
        UI.button(80, y, bw, 80, T('paste'), {id = 'paste', icon = 'BUTTON_MENU', color = C.play, tap = function()
            local r = LM.host('paste')
            Hub.load(r.text)
        end})
        UI.button(w - 80 - bw, y, bw, 80, T('picture'), {id = 'picture', icon = 'BUTTON_FULLSCREEN', color = C.play, tap = function()
            Hub.wait('open_picture', function(r)
                if r.code then Hub.load(r.code)
                elseif r.error then Hub.message(T(r.error) or r.error) end
            end)
        end})
    else
        UI.button(80, y, w - 160, 80, T('new_level'), {id = 'new', icon = 'BUTTON_EMBLEM_EDIT', color = C.play, tap = Hub.newLevel})
    end
    y = y + 110
    local slots = LM.store().slots
    UI.label(T('my_levels') .. '  (' .. #slots .. ' / ' .. LM.MAX_SLOTS .. ')', 80, y, 30, 'FONT_BASIC', 'LEFT')
    y = y + 30
    local pages = math.max(1, math.ceil(#slots / PER_PAGE))
    page.page = math.max(1, math.min(page.page or 1, pages))
    local cols = 4
    local tw, th = (w - 160 - (cols - 1) * 20) / cols, 120
    for i = 1, PER_PAGE do
        local n = (page.page - 1) * PER_PAGE + i
        local s = slots[n]
        if not s then break end
        local col, row = (i - 1) % cols, math.floor((i - 1) / cols)
        local x, ty = 80 + col * (tw + 20), y + row * (th + 16)
        UI.button(x, ty, tw, th, nil, {id = 'slot' .. n, color = C.button, tap = function()
            if page.mode == 'load' then Hub.openLevel(n)
            else
                local L, err = LM.decode(s.code)
                if not L then return Hub.message(T('damaged')) end
                Hub.edit({slot = n, level = L, code = s.code})
            end
        end})
        UI.label(T('level') .. ' ' .. n, x + tw * 0.5, ty + 30, 30, 'FONT_HEADER')
        stars_row(s.stars, x + tw * 0.5, ty + 68, 30)
        UI.label((s.best or 0) > 0 and tostring(s.best) or (s.beaten and '' or T('not_beaten')), x + tw * 0.5, ty + 100, 22)
    end
    if #slots == 0 then UI.label(T('no_levels'), w * 0.5, y + 120, 28) end
    if pages > 1 then
        UI.button(80, H - 110, 90, 60, nil, {id = 'prev', icon = 'BUTTON_ARROW_LEFT', enabled = page.page > 1, tap = function() page.page = page.page - 1 end})
        UI.button(w - 170, H - 110, 90, 60, nil, {id = 'next', icon = 'BUTTON_ARROW_RIGHT', enabled = page.page < pages, tap = function() page.page = page.page + 1 end})
        UI.label(page.page .. ' / ' .. pages, w * 0.5, H - 80, 28)
    end
end

function draw.level(page)
    local w = W()
    local L = page.level
    local saved = page.slot ~= nil
    header(saved and (T('level') .. ' ' .. page.slot) or T('loaded_level'))
    local s = saved and LM.store().slots[page.slot] or nil
    local pigs, birds = stats(L)
    local y = 150
    UI.label(T('pigs') .. ': ' .. pigs .. '    ' .. T('birds') .. ': ' .. birds .. '    ' .. T('objects') .. ': ' .. #L.objects,
             w * 0.5, y, 28)
    if s then
        stars_row(s.stars, w * 0.5, y + 50, 40)
        UI.label(T('best') .. ': ' .. tostring(s.best or 0), w * 0.5, y + 95, 26)
    end
    UI.button(w * 0.5 - 200, 290, 400, 170, T('play'), {id = 'play', size = 64, icon = 'GESTURE_PLAY', color = C.play, tap = function() Hub.play(page) end})
    local beaten = s and s.beaten == page.code
    local bw, by = 220, 500
    local buttons = {}
    if not saved then buttons[#buttons + 1] = {T('save'), function() Hub.save(page) end, nil, 'CHECKMARK'} end
    buttons[#buttons + 1] = {T('edit'), function() Hub.edit(page) end, nil, 'BUTTON_EMBLEM_EDIT'}
    buttons[#buttons + 1] = {T('share'), function()
        if beaten then Hub.push({kind = 'share', code = page.code}) else Hub.message(T('beat_first')) end
    end, beaten and C.button or C.off, 'BUTTON_EXTERNAL_LINK'}
    if saved then buttons[#buttons + 1] = {T('delete'), function()
        Hub.confirm(T('delete_confirm'), function()
            table.remove(LM.store().slots, page.slot)
            LM.saveStore()
            Hub.pop()
        end)
    end, C.danger, 'MENU_NO'} end
    local total = #buttons * bw + (#buttons - 1) * 20
    for i, b in ipairs(buttons) do
        UI.button(w * 0.5 - total * 0.5 + (i - 1) * (bw + 20), by, bw, 80, b[1], {id = 'b' .. i, color = b[3], icon = b[4], tap = b[2]})
    end
    if not beaten then UI.label(T('beat_first'), w * 0.5, by + 120, 22) end
end

function draw.share(page)
    local w = W()
    header(T('share'))
    if not page.qr then
        local r = LM.host('qr', page.code)
        page.qr, page.size = r.modules or '', r.size or 0
    end
    local size = 400
    local x0, y0 = 100, 140
    UI.panel(x0 - 10, y0 - 10, size + 20, size + 20, 'paper')
    UI.box(x0 + 10, y0 + 10, size - 20, size - 20, {1, 1, 1, 1})
    if page.size > 0 then
        -- whole screen pixels a module, so neighbouring modules meet without seams
        local k = UI.k()
        local m = math.max(1, math.floor((size - 40) * k / page.size))
        local X = math.floor((x0 + size * 0.5) * k - m * page.size * 0.5)
        local Y = math.floor((y0 + size * 0.5) * k - m * page.size * 0.5)
        setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
        for row = 0, page.size - 1 do
            local base = row * page.size
            local col = 0
            while col < page.size do
                if string.byte(page.qr, base + col + 1) == 49 then
                    local run = col
                    while run + 1 < page.size and string.byte(page.qr, base + run + 2) == 49 do run = run + 1 end
                    drawRect(0, 0, 0, 1, X + col * m, Y + row * m, X + (run + 1) * m, Y + (row + 1) * m, true)
                    col = run + 1
                else
                    col = col + 1
                end
            end
        end
    else
        UI.label(T('too_big'), x0 + size * 0.5, y0 + size * 0.5, 26, 'FONT_BASIC', 'HCENTER', 'VCENTER', size - 20)
    end
    local x = x0 + size + 60
    local bw = w - x - 80
    UI.label(T('share_hint'), x + bw * 0.5, 170, 24, 'FONT_BASIC', 'HCENTER', 'VCENTER', bw)
    UI.label(string.len(page.code) .. ' ' .. T('characters'), x + bw * 0.5, 210, 24)
    UI.button(x, 260, bw, 90, T('copy'), {id = 'copy', icon = 'BUTTON_MENU', color = C.play, tap = function()
        local r = LM.host('copy', page.code)
        Hub.message(r.ok and T('copied') or T('failed'))
    end})
    UI.button(x, 380, bw, 90, T('save_picture'), {id = 'savepic', icon = 'BUTTON_FULLSCREEN', enabled = page.size > 0, tap = function()
        Hub.wait('save_picture\n' .. page.code, function(r)
            if r.ok then Hub.message(T('saved')) elseif r.error then Hub.message(T(r.error) or r.error) end
        end)
    end})
end

function draw.message(page)
    local w = W()
    UI.box(0, 0, w, H, C.dim)
    UI.panel(w * 0.5 - 360, 220, 720, 280, 'popup')
    UI.label(page.text, w * 0.5, 320, 30, 'FONT_BASIC', 'HCENTER', 'VCENTER', 660)
    UI.button(w * 0.5 - 100, 400, 200, 76, T('ok'), {id = 'ok', icon = 'MENU_YES', color = C.play, tap = function()
        Hub.pop()
        if page.after then page.after() end
    end})
end

function draw.confirm(page)
    local w = W()
    UI.box(0, 0, w, H, C.dim)
    UI.panel(w * 0.5 - 360, 220, 720, 280, 'popup')
    UI.label(page.text, w * 0.5, 320, 30, 'FONT_BASIC', 'HCENTER', 'VCENTER', 660)
    UI.button(w * 0.5 - 230, 400, 200, 76, T('yes'), {id = 'yes', icon = 'MENU_YES', color = C.danger, tap = function() Hub.pop() page.yes() end})
    UI.button(w * 0.5 + 30, 400, 200, 76, T('no'), {id = 'no', icon = 'MENU_NO', tap = Hub.pop})
end

function draw.wait(page)
    local w = W()
    if not page.started then
        page.started = true
        local cmd, arg = string.match(page.cmd, '^([^\n]+)\n?(.*)$')
        local r = LM.host(cmd, arg)
        if not r.pending then Hub.pop() page.done(r) return end
    else
        local r = LM.host('result')
        if not r.pending then Hub.pop() page.done(r) return end
    end
    UI.box(0, 0, w, H, C.dim)
    UI.label(T('waiting'), w * 0.5, H * 0.5, 34)
end

function Hub.build(screen)
    local page = Hub.page
    if not page then return end
    draw[page.kind](page)
end

-- ---- opening and closing (on the main menu)
function Hub.open(menu, page)
    if Hub.screen and Hub.menu == menu then return end
    Hub.stack = {}
    Hub.push({kind = 'home'})
    if page then Hub.push(page) end
    local screen = UI.Screen:new({name = 'levelMakerScreen', build = Hub.build})
    Hub.screen, Hub.menu = screen, menu
    UI.onBack = function() Hub.pop() end
    UI.onWheel = nil
    menu:addChild(screen)
end

function Hub.close()
    if Hub.screen and Hub.menu then Hub.menu:removeChild(Hub.screen) end
    Hub.screen, Hub.menu = nil, nil
    UI.onBack = nil
end

-- After a played level: the main menu opens the level maker again.
function Hub.reopen(menu)
    if not LM.reopen then return end
    local from = LM.reopen
    LM.reopen = false
    local page = from == 'build' and {kind = 'list', mode = 'build', page = 1} or nil
    if LM.slot and LM.store().slots[LM.slot] then
        local L = LM.decode(LM.store().slots[LM.slot].code)
        if L then page = {kind = 'level', slot = LM.slot, level = L, code = LM.store().slots[LM.slot].code} end
    elseif LM.level and LM.code and from ~= 'build' then
        page = {kind = 'level', level = LM.level, code = LM.code}
    end
    Hub.open(menu, page)
end
)";

}  // namespace slingshot::level_maker
