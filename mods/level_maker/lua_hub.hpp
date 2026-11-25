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
    local top = Hub.stack[#Hub.stack]
    if top and top.leave then top.leave() end
    Hub.stack[#Hub.stack] = nil
    Hub.page = Hub.stack[#Hub.stack]
    if not Hub.page then Hub.close() end
end
function Hub.message(text, after) Hub.push({kind = 'message', text = text, after = after}) end
function Hub.confirm(text, yes) Hub.push({kind = 'confirm', text = text, yes = yes}) end

local function stats(L)
    local pigs, birds, other = 0, 0, 0
    for _, o in ipairs(L.objects) do
        if LM.isPig(o.def) then pigs = pigs + 1 elseif LM.isBird(o.def) then birds = birds + 1 else other = other + 1 end
    end
    return pigs, birds, other
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
-- A box of pw x ph in the middle with its title and close button; returns its left and top.
local function popup(title, pw, ph, back)
    local w = W()
    local px, py = math.floor((w - pw) * 0.5), math.floor((H - ph) * 0.5)
    UI.box(0, 0, w, H, C.dim)
    UI.panel(px, py, pw, ph, 'popup')
    UI.label(title, w * 0.5, py + 52, 44, 'FONT_HEADER', 'HCENTER', 'VCENTER', pw - 200)
    UI.button(px + pw - 78, py + 14, 64, 64, nil, {id = 'close', icon = 'MENU_NO', tap = back or Hub.pop})
    return px, py
end

local function stars_row(n, x, y, size)
    for i = 1, 3 do
        local sx = x + (i - 2) * size * 1.05
        if not UI.icon(i <= (n or 0) and 'UI_SCORE_STAR_YELLOW' or 'UI_SCORE_STAR_GREY', sx, y, size, size, 0, 1.2) then
            UI.label(i <= (n or 0) and '*' or '-', sx, y, size)
        end
    end
end

-- Pictures on an empty round button, white with a brown edge as the game's icons are.
local BROWN, WHITE = {0.45, 0.22, 0.05}, {1, 1, 1}
-- a QR code: its three corner squares
local function qr_icon(x, y)
    for _, c in ipairs({{-1, -1}, {1, -1}, {-1, 1}}) do
        local cx, cy = x + c[1] * 11, y + c[2] * 11
        UI.round(cx - 9, cy - 9, 18, 18, BROWN, 1, 4)
        UI.round(cx - 6, cy - 6, 12, 12, WHITE, 1, 3)
        UI.round(cx - 4, cy - 4, 8, 8, BROWN, 1, 2)
    end
    UI.round(x + 5, y + 5, 12, 12, WHITE, 1, 2)
end
-- a camera
local function camera_icon(x, y)
    UI.round(x - 10, y - 20, 20, 10, BROWN, 1, 3)
    UI.round(x - 24, y - 14, 48, 32, BROWN, 1, 7)
    UI.round(x - 21, y - 11, 42, 26, WHITE, 1, 5)
    UI.round(x - 10, y - 8, 20, 20, BROWN, 1, 10)
    UI.round(x - 6, y - 4, 12, 12, WHITE, 1, 6)
end

local draw = {}

-- The shop's tiles (its sheet is loaded while the hub is open), at the game's own size.
local TILES = {load = 'SHOP_MAIN_GEMS_COINS_BOX', build = 'SHOP_BG_TALL_BLUE'}
function draw.home()
    local w = W()
    local nat = UI.native()
    local tw, th = 237 * nat, 348 * nat
    local gap, cell = 50, math.max(tw, 270)
    local pw, ph = cell * 2 + gap + 80, th + 190
    local px, py = (w - pw) * 0.5, (H - ph) * 0.5
    UI.box(0, 0, w, H, C.dim)
    UI.panel(px, py, pw, ph, 'popup')
    UI.label(T('title'), w * 0.5, py + 52, 44, 'FONT_HEADER')
    UI.button(px + pw - 78, py + 14, 64, 64, nil, {id = 'close', icon = 'MENU_NO', tap = Hub.close})
    for i, it in ipairs({
        {'load', 'GESTURE_PLAY', T('load'), T('load_hint'), function() Hub.push({kind = 'list', mode = 'load', page = 1}) end},
        {'build', 'BUTTON_EMBLEM_EDIT', T('build'), T('build_hint'), function() Hub.push({kind = 'list', mode = 'build', page = 1}) end},
    }) do
        local cx = px + 40 + cell * 0.5 + (i - 1) * (cell + gap)
        local x, y = cx - tw * 0.5, py + 100
        local pressed = UI.pressed and UI.pressed.id == it[1]
        local f = pressed and 0.95 or 1
        if not UI.sprite(TILES[it[1]], cx, y + th * 0.5, tw * f, th * f) then
            UI.panel(x, y, tw, th, 'paper', it[1] == 'load' and {0.55, 0.9, 0.4} or {0.5, 0.72, 1})
        end
        UI.icon(it[2], cx, y + th * 0.4, tw, th, 0, 1.2)
        UI.label(it[3], cx, y + th * 0.78, 44, 'FONT_HEADER', 'HCENTER', 'VCENTER', tw - 20)
        UI.area(x, y, tw, th, {id = it[1], tap = it[5]})
        UI.wrap(it[4], cx, y + th + 22, 22, 'FONT_BASIC', cell + 20)
    end
end

-- The saved levels as the game's level buttons, 8 x 2 a page.
local COLS, ROWS = 8, 2
local PER_PAGE = COLS * ROWS
function draw.list(page)
    local w = W()
    local cell, rowh = 110, 150
    local pw, ph = COLS * cell + 160, 650
    local px, py = popup(page.mode == 'load' and T('load') or T('build'), pw, ph)
    local cx = px + pw * 0.5
    -- what makes a new entry: round buttons with their names
    local actions
    if page.mode == 'load' then
        actions = {
            {'paste', 'BUTTON_MENU', T('paste'), function() Hub.load(LM.host('paste').text) end},
            {'picture', 'BUTTON_EMPTY', T('picture'), function()
                Hub.wait('open_picture', function(r)
                    if r.code then Hub.load(r.code)
                    elseif r.error then Hub.message(T(r.error) or r.error) end
                end)
            end},
        }
        -- the camera, where the device has one
        if Hub.camera == nil then
            local r = LM.host('camera_available')
            if not r.pending then Hub.camera = r.ok == true end
        end
        if Hub.camera then
            table.insert(actions, 2, {'scan', 'BUTTON_EMPTY', T('scan'), function() Hub.push({kind = 'scan'}) end})
        end
    else
        actions = {{'new', 'BUTTON_EMBLEM_EDIT', T('new_level'), Hub.newLevel}}
    end
    for i, a in ipairs(actions) do
        local ax = cx + (i - (#actions + 1) * 0.5) * 260
        UI.choice(ax, py + 150, 92, a[2], a[3], {id = a[1], size = 26, maxw = 250, tap = a[4]})
        if a[1] == 'picture' then qr_icon(ax, py + 147) elseif a[1] == 'scan' then camera_icon(ax, py + 149) end
    end
    local slots = LM.store().slots
    UI.label(T('my_levels') .. '  (' .. #slots .. ' / ' .. LM.MAX_SLOTS .. ')', cx, py + 262, 26)
    local pages = math.max(1, math.ceil(#slots / PER_PAGE))
    page.page = math.max(1, math.min(page.page or 1, pages))
    local gx, gy = cx - COLS * cell * 0.5, py + 290
    for i = 1, PER_PAGE do
        local n = (page.page - 1) * PER_PAGE + i
        local s = slots[n]
        if not s then break end
        local x = gx + ((i - 1) % COLS) * cell + cell * 0.5
        local y = gy + math.floor((i - 1) / COLS) * rowh + 52
        local pressed = UI.pressed and UI.pressed.id == 'slot' .. n
        UI.icon('LEVEL_BUTTON_EMPTY', x, y, pressed and 90 or 96, pressed and 90 or 102, 0)
        UI.label(tostring(n), x, y - 4, 44, 'FONT_HEADER')
        stars_row(s.stars, x, y + 66, 24)
        UI.area(x - 48, y - 51, 96, 102, {id = 'slot' .. n, tap = function()
            if page.mode == 'load' then Hub.openLevel(n)
            else
                local L = LM.decode(s.code)
                if not L then return Hub.message(T('damaged')) end
                Hub.edit({slot = n, level = L, code = s.code})
            end
        end})
    end
    if #slots == 0 then UI.label(T('no_levels'), cx, gy + 120, 28) end
    if pages > 1 then
        local ay = gy + rowh
        UI.button(px + 20, ay - 40, 40, 80, nil, {id = 'prev', icon = 'BUTTON_LEVELSELECT_ARROW_LEFT', enabled = page.page > 1, tap = function() page.page = page.page - 1 end})
        UI.button(px + pw - 60, ay - 40, 40, 80, nil, {id = 'next', icon = 'BUTTON_LEVELSELECT_ARROW_RIGHT', enabled = page.page < pages, tap = function() page.page = page.page + 1 end})
        UI.label(page.page .. ' / ' .. pages, cx, py + ph - 28, 24)
    end
end

-- One number with its picture (the level's pigs, birds: the score's icons, else the element's).
local function count(sprites, n, x, y)
    local shown = false
    for _, sprite in ipairs(sprites) do
        if not shown then shown = UI.icon(sprite, x - 28, y, 46, 46, 0, 1.2) end
    end
    if shown then UI.label(tostring(n), x + 6, y, 36, 'FONT_HEADER', 'LEFT')
    else UI.label(tostring(n), x, y, 36, 'FONT_HEADER') end
end


function draw.level(page)
    local L = page.level
    local saved = page.slot ~= nil
    local s = saved and LM.store().slots[page.slot] or nil
    local beaten = s and s.beaten == page.code
    local pw, ph = 820, 630
    local px, py = popup(saved and (T('level') .. ' ' .. page.slot) or T('loaded_level'), pw, ph)
    local cx = px + pw * 0.5
    local pigs, birds, other = stats(L)
    stars_row(s and s.stars or 0, cx, py + 130, 40)
    if s then UI.label(T('best') .. ': ' .. tostring(s.best or 0), cx, py + 182, 24) end
    -- on the left: pigs, birds, the other elements
    local blocks = blockTable.blocks
    count({'UI_SCORE_PIG', blocks.MediumPiglette.sprite}, pigs, px + 110, py + 140)
    count({blocks.RedBird.sprite, 'SOUNDBOARD_1_BIRD_RED', 'UI_SCORE_BIRD'}, birds, px + 110, py + 205)
    count({'SOUNDBOARD_1_BLOCK_WOOD', blocks.WoodBlock9.sprite}, other, px + 110, py + 270)
    UI.choice(cx, py + 295, 120, 'GESTURE_PLAY', T('play'), {id = 'play', size = 40, tap = function() Hub.play(page) end})
    local buttons = {}
    if not saved then buttons[#buttons + 1] = {'save', 'MENU_YES', T('save'), function() Hub.save(page) end} end
    buttons[#buttons + 1] = {'edit', 'BUTTON_EMBLEM_EDIT', T('edit'), function() Hub.edit(page) end}
    buttons[#buttons + 1] = {'share', 'BUTTON_EXTERNAL_LINK', T('share'), function()
        if beaten then Hub.push({kind = 'share', code = page.code}) else Hub.message(T('beat_first')) end
    end, not beaten}
    if saved then buttons[#buttons + 1] = {'delete', 'MENU_NO', T('delete'), function()
        Hub.confirm(T('delete_confirm'), function()
            table.remove(LM.store().slots, page.slot)
            LM.saveStore()
            Hub.pop()
        end)
    end} end
    for i, b in ipairs(buttons) do
        UI.choice(cx + (i - (#buttons + 1) * 0.5) * 170, py + 490, 84, b[2], b[3], {id = b[1], size = 24, maxw = 165, grey = b[5], tap = b[4]})
    end
    if not beaten then UI.label(T('beat_first'), cx, py + ph - 26, 22, 'FONT_BASIC', 'HCENTER', 'VCENTER', pw - 60) end
end

function draw.share(page)
    local pw, ph = 900, 560
    local px, py = popup(T('share'), pw, ph)
    if not page.qr then
        local r = LM.host('qr', page.code)
        page.qr, page.size = r.modules or '', r.size or 0
    end
    local size = 400
    local x0, y0 = px + 50, py + 110
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
    local x = x0 + size + 40
    local bw = px + pw - 40 - x
    local cx = x + bw * 0.5
    UI.wrap(T('share_hint'), cx, y0 + 20, 24, 'FONT_BASIC', bw)
    UI.label(string.len(page.code) .. ' ' .. T('characters'), cx, y0 + 110, 24)
    UI.choice(cx - 95, y0 + 230, 92, 'BUTTON_MENU', T('copy'), {id = 'copy', size = 24, maxw = 180, tap = function()
        local r = LM.host('copy', page.code)
        Hub.message(r.ok and T('copied') or T('failed'))
    end})
    UI.choice(cx + 95, y0 + 230, 92, 'BUTTON_EMPTY', T('save_picture'), {id = 'savepic', size = 24, maxw = 180, enabled = page.size > 0, tap = function()
        Hub.wait('save_picture\n' .. page.code, function(r)
            if r.ok then Hub.message(T('saved')) elseif r.error then Hub.message(T(r.error) or r.error) end
        end)
    end})
    qr_icon(cx + 95, y0 + 227)
end

-- The camera's picture (drawn by the host over the game in the frame given each frame)
-- until it reads a QR code.
function draw.scan(page)
    local pw, ph = 900, 640
    local px, py = popup(T('scan'), pw, ph)
    if not page.started then
        page.started = true
        LM.host('camera_open')
        page.leave = function() LM.host('camera_close') end
    end
    local x, y, w, h = px + 40, py + 96, pw - 80, (pw - 80) * 9 / 16
    UI.box(x, y, w, h, {0, 0, 0, 1})
    local k = UI.k()
    local r = LM.host('camera', string.format('%d %d %d %d', x * k, y * k, w * k, h * k))
    if r.code then
        Hub.pop()
        return Hub.load(r.code)
    end
    if r.state == 'denied' or r.state == 'none' then
        Hub.pop()
        return Hub.message(T(r.state == 'denied' and 'camera_denied' or 'no_camera'))
    end
    if r.state ~= 'running' then UI.label(T('camera_waiting'), x + w * 0.5, y + h * 0.5, 30) end
    UI.wrap(T('scan_hint'), px + pw * 0.5, y + h + 30, 26, 'FONT_BASIC', pw - 80)
end

function draw.message(page)
    local w = W()
    UI.box(0, 0, w, H, C.dim)
    UI.panel(w * 0.5 - 380, 170, 760, 380, 'popup')
    UI.label(page.text, w * 0.5, 270, 32, 'FONT_BASIC', 'HCENTER', 'VCENTER', 680)
    UI.choice(w * 0.5, 410, 120, 'MENU_YES', T('ok'), {id = 'ok', tap = function()
        Hub.pop()
        if page.after then page.after() end
    end})
end

function draw.confirm(page)
    local w = W()
    UI.box(0, 0, w, H, C.dim)
    UI.panel(w * 0.5 - 380, 170, 760, 380, 'popup')
    UI.label(page.text, w * 0.5, 260, 32, 'FONT_BASIC', 'HCENTER', 'VCENTER', 680)
    UI.choice(w * 0.5 - 130, 400, 120, 'MENU_YES', T('yes'), {id = 'yes', tap = function() Hub.pop() page.yes() end})
    UI.choice(w * 0.5 + 130, 400, 120, 'MENU_NO', T('no'), {id = 'no', tap = Hub.pop})
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
    if dynamic and dynamic.load then pcall(dynamic.load, 'mainShopAssets') end
end

function Hub.close()
    for _, p in ipairs(Hub.stack or {}) do if p.leave then p.leave() end end
    if Hub.screen and Hub.menu then
        Hub.menu:removeChild(Hub.screen)
        if dynamic and dynamic.release then pcall(dynamic.release, 'mainShopAssets') end
    end
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
