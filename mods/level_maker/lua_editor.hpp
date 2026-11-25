// SPDX-License-Identifier: GPL-3.0-or-later
// level-maker: the editor, in the level's own scene with physics off.
//
// Elements come from a bar of categories at the bottom; dragged from there (or
// tapped: placed in the middle) they snap to the edges of their neighbours and the
// ground and never overlap: a dropped element that would sits on top instead. A tap
// selects; the selection has rotate, copy and delete, and long press / right click /
// the gamepad's X open the rest (fine rotation, layers, connections). Dragging
// empty space moves the view; two fingers, the wheel and the buttons zoom.
#pragma once

namespace slingshot::level_maker {

constexpr const char* kEditorLua = R"(
local G = _G
local LM = G.levelMaker
local UI = LM.UI
local T = UI.text
local C = UI.COLORS
local ED = LM.Editor or {}
LM.Editor = ED

local function W() return (screenWidth or 1280) / UI.k() end
local H = 720

-- ---- the palette
ED.CATEGORIES = {
    {id = 'wood', groups = {'woodBlocks'}, icon = 'WoodBlock4'},
    {id = 'ice', groups = {'lightBlocks'}, icon = 'LightBlock4'},
    {id = 'stone', groups = {'rockBlocks'}, icon = 'StoneBlock4'},
    {id = 'pigs', groups = {'piglettes'}, icon = 'MediumPiglette'},
    {id = 'birds', groups = {'birds'}, icon = 'RedBird'},
    {id = 'tnt', groups = {'extras'}, icon = 'ExtraBlockTNT'},
    {id = 'terrain', groups = {'staticBlocks', 'staticFragile', 'staticIndestructible'}, icon = 'StaticBlock_01'},
    {id = 'deco', groups = {'decorations', 'nocollision'}, icon = 'UGC_GLASSES'},
    {id = 'special', groups = {'scoreObjects', 'opobjects', 'dino', 'farm', 'badPiggiesVehicles', 'fbitems'}, icon = 'ExtraGoldenEgg'},
}
-- Not offered: the ground (always there), and birds of the Mighty Eagle and of
-- power-ups, which need those (MightyEagleBird and YellowBirdSpeed stop the game).
local HIDDEN = {
    Ground = true, EmptyPlaceholderBird = true, MightyEagleBird = true, BaitSardine = true, YellowBirdSpeed = true,
    TNT_BIRD = true, RedsMightyFeathersBird = true, BlackBird_Shockwave = true, BlackBird_Shockwave_Cage = true,
}

function LM.paletteDefs()
    local defs = {}
    local groups = {}
    for _, c in ipairs(ED.CATEGORIES) do for _, g in ipairs(c.groups) do groups[g] = true end end
    for name, d in pairs(blockTable.blocks) do
        if type(d) == 'table' and groups[d.group] and not HIDDEN[name] and type(d.sprite) == 'string' then defs[name] = true end
    end
    return defs
end

-- Plain numbered elements (WoodBlock1, WoodBlock2...) first, in number order, then the rest.
local function tokens(s)
    local t = {}
    for text, digits in string.gmatch(s, '(%D*)(%d*)') do
        if text ~= '' then t[#t + 1] = text end
        if digits ~= '' then t[#t + 1] = tonumber(digits) end
    end
    return t
end
local function natural(a, b, prefix)
    if prefix then
        local fa, fb = string.match(a, '^' .. prefix .. '%d+$') ~= nil, string.match(b, '^' .. prefix .. '%d+$') ~= nil
        if fa ~= fb then return fa end
    end
    local pa, pb = string.match(a, '^%a+%d*$') ~= nil, string.match(b, '^%a+%d*$') ~= nil
    if pa ~= pb then return pa end
    local ta, tb = tokens(a), tokens(b)
    for i = 1, math.max(#ta, #tb) do
        local x, y = ta[i], tb[i]
        if x == nil then return true end
        if y == nil then return false end
        if type(x) ~= type(y) then return type(x) == 'number' end
        if x ~= y then return x < y end
    end
    return false
end

local function build_palette()
    local blocks = blockTable.blocks
    for _, c in ipairs(ED.CATEGORIES) do
        c.tabs = {}
        for _, g in ipairs(c.groups) do
            local items = {}
            for name, d in pairs(blocks) do
                if type(d) == 'table' and d.group == g and not HIDDEN[name] and type(d.sprite) == 'string' then
                    local w = res.getSpriteBounds('', d.sprite)
                    if w and w > 0 then items[#items + 1] = name end
                end
            end
            local prefix = c.icon and string.match(c.icon, '^(%a+)%d+$')
            table.sort(items, function(a, b) return natural(a, b, prefix) end)
            if #items > 0 then c.tabs[#c.tabs + 1] = {group = g, items = items} end
        end
        c.tab = c.tabs[1]
        if not c.icon or not blocks[c.icon] then c.icon = c.tab and c.tab.items[1] end
    end
end

-- ---- geometry (physics units, y down)
local drop, in_sling
local function live(o) return o.name and objects.world[o.name] end

-- Half extents of an element and of its axis-aligned box.
local function shape(o)
    local w = live(o)
    if not w then return nil end
    -- boxes have a width, circles a radius; polygons (-1 for both) their original size
    local hw, hh, round
    if (w.width or 0) > 0 and (w.height or 0) > 0 then hw, hh = w.width * 0.5, w.height * 0.5
    elseif (w.radius or 0) > 0 then hw, hh, round = w.radius, w.radius, true
    elseif (w.originalWidth or 0) > 0 and (w.originalHeight or 0) > 0 then hw, hh = w.originalWidth * 0.5, w.originalHeight * 0.5
    else hw, hh = 0.5, 0.5 end
    local a = w.angle or o.angle or 0
    local c, s = math.abs(math.cos(a)), math.abs(math.sin(a))
    return {x = w.x, y = w.y, angle = a, hw = hw, hh = hh, ex = c * hw + s * hh, ey = s * hw + c * hh, round = round}
end

local function corners(s)
    local c, n = math.cos(s.angle), math.sin(s.angle)
    local pts = {}
    for _, p in ipairs({{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) do
        local lx, ly = p[1] * s.hw, p[2] * s.hh
        pts[#pts + 1] = {s.x + lx * c - ly * n, s.y + lx * n + ly * c}
    end
    return pts
end

-- Separating axes of two (rotated) boxes: true when they overlap by more than a little.
local function overlap(a, b)
    if math.abs(a.x - b.x) > a.ex + b.ex or math.abs(a.y - b.y) > a.ey + b.ey then return false end
    local pa, pb = corners(a), corners(b)
    for _, s in ipairs({a, b}) do
        for i = 0, 1 do
            local ang = s.angle + i * math.pi * 0.5
            local ax, ay = math.cos(ang), math.sin(ang)
            local mina, maxa, minb, maxb = 1e9, -1e9, 1e9, -1e9
            for _, p in ipairs(pa) do local d = p[1] * ax + p[2] * ay mina = math.min(mina, d) maxa = math.max(maxa, d) end
            for _, p in ipairs(pb) do local d = p[1] * ax + p[2] * ay minb = math.min(minb, d) maxb = math.max(maxb, d) end
            if maxa - minb < 0.04 or maxb - mina < 0.04 then return false end
        end
    end
    return true
end

local function is_ground(o) local d = blockTable.blocks[o.def] return d and d.group == 'ground' end
local function solid(o)
    local d = blockTable.blocks[o.def]
    return d and d.group ~= 'decorations' and d.group ~= 'nocollision'
end

-- Units per screen pixel.
local function unit()
    local x1 = screenToPhysicsTransform(0, 0)
    local x2 = screenToPhysicsTransform(100, 0)
    return math.abs(x2 - x1) / 100
end

-- Snaps a moved element's centre to edges of the others near it.
local function snap(m, x, y)
    local ms = shape(m)
    if not ms then return x, y end
    local thr = 14 * unit() / UI.k() * UI.k()
    local bx, by, dx, dy = x, y, thr, thr
    for _, o in ipairs(ED.L.objects) do
        if o ~= m and solid(o) then
            local s = shape(o)
            if s then
                local l, r, t, b = s.x - s.ex, s.x + s.ex, s.y - s.ey, s.y + s.ey
                if math.abs(y - s.y) < ms.ey + s.ey + thr then
                    for _, cx in ipairs({r + ms.ex, l - ms.ex, l + ms.ex, r - ms.ex, s.x}) do
                        if math.abs(cx - x) < dx then dx, bx = math.abs(cx - x), cx end
                    end
                end
                if math.abs(x - s.x) < ms.ex + s.ex + thr or is_ground(o) then
                    local list = is_ground(o) and {t - ms.ey} or {t - ms.ey, b + ms.ey, t + ms.ey, b - ms.ey, s.y}
                    for _, cy in ipairs(list) do
                        if math.abs(cy - y) < dy then dy, by = math.abs(cy - y), cy end
                    end
                end
            end
        end
    end
    return bx, by
end

local function collides(m)
    local ms = shape(m)
    if not ms or not solid(m) then return nil end
    for _, o in ipairs(ED.L.objects) do
        if o ~= m and solid(o) then
            local s = shape(o)
            if s and overlap(ms, s) then return o, s end
        end
    end
end

local function place(o, x, y)
    o.x, o.y = x, y
    if o.name then setPosition(o.name, x, y) end
end

-- A dropped element that overlaps others goes on top of them, else back to where it was.
local function settle(o, fromx, fromy)
    for _ = 1, 12 do
        local other, s = collides(o)
        if not other then return true end
        local ms = shape(o)
        place(o, o.x, s.y - s.ey - ms.ey - 0.01)
    end
    if fromx then place(o, fromx, fromy) else return false end
    return not collides(o)
end

-- The room of the slingshot and its bird (units around the slingshot's place).
local SLING_ROOM = {hw = 1.3, up = 5.5, down = 0.6}
local function sling_room()
    local sling = LM.slingPos(ED.L)
    local x, y = sling[1] / 20, sling[2] / 20
    local hh = (SLING_ROOM.up + SLING_ROOM.down) * 0.5
    return {x = x, y = y - SLING_ROOM.up + hh, angle = 0, hw = SLING_ROOM.hw, hh = hh, ex = SLING_ROOM.hw, ey = hh}
end
-- Whether o is in the slingshot's room (bird 1 belongs there), or, for bird 1, whether
-- anything solid is.
in_sling = function(o)
    local first = LM.birdOrder(ED.L)[1]
    local room = sling_room()
    if o == first then
        room.x, room.y = o.x, o.y - SLING_ROOM.up + room.hh
        for _, other in ipairs(ED.L.objects) do
            if other ~= o and solid(other) and not is_ground(other) and not LM.isBird(other.def) then
                local s = shape(other)
                if s and overlap(room, s) then return true end
            end
        end
        return false
    end
    if not solid(o) or is_ground(o) or LM.isBird(o.def) then return false end
    local s = shape(o)
    return s ~= nil and overlap(room, s)
end

-- Down onto whatever is below (the ground at least).
drop = function(o)
    local ms = shape(o)
    if not ms then return end
    local best
    for _, other in ipairs(ED.L.objects) do
        if other ~= o and solid(other) then
            local s = shape(other)
            if s and (is_ground(other) or math.abs(o.x - s.x) < ms.ex + s.ex - 0.02) then
                local top = s.y - s.ey
                if top > o.y and (not best or top < best) then best = top end
            end
        end
    end
    if best then place(o, o.x, best - ms.ey - 0.01) end
end

ED.drop = function(o) return drop(o) end

-- ---- changes, undo
-- The level's settings an undo step keeps besides its objects.
local SETTINGS = {'theme', 'gravity', 'particles', 'music', 'limits', 'boom'}
local function snapshot()
    local s = {objects = {}, joints = LM.copy(ED.L.joints), settings = {}}
    for i, o in ipairs(ED.L.objects) do
        s.objects[i] = {def = o.def, x = o.x, y = o.y, angle = o.angle, z = o.z, extra = LM.copy(o.extra)}
    end
    for _, k in ipairs(SETTINGS) do s.settings[k] = LM.copy(ED.L[k]) end
    return s
end

local function changed()
    LM.editorBeaten = nil
    ED.dirty = true
end

function ED.remember()
    ED.undo[#ED.undo + 1] = snapshot()
    if #ED.undo > 60 then table.remove(ED.undo, 1) end
    ED.redo = {}
end

local function create(o)
    local d = blockTable.blocks[o.def]
    local name = createObject(blockTable, o.def, nil, o.x, o.y, {angle = o.angle, z_order = o.z})
    o.name = name
    return name ~= nil
end

local function restore(s)
    local L = ED.L
    local theme, music = L.theme, LM.musicOf(L)
    for _, k in ipairs(SETTINGS) do L[k] = LM.copy(s.settings[k]) end
    if LM.musicOf(L) ~= music then LM.playMusic(L) end
    if L.theme ~= theme then
        -- another background: the scene is built again, the view where the level now stands
        ED.L.objects, ED.L.joints = s.objects, s.joints
        ED.cam.y = ED.cam.y + (LM.surfaceOf(L.theme) - LM.surfaceOf(theme)) * 20
        local first = LM.birdOrder(L)[1]
        if first then L.sling = {first.x * 20, first.y * 20} end
        changed()
        ED.rebuild()
        return
    end
    for _, o in ipairs(ED.L.objects) do
        if o.name and not is_ground(o) then removeObject(o.name) end
    end
    local ground = {}
    for _, o in ipairs(ED.L.objects) do if is_ground(o) then ground[o.def .. o.x .. o.y] = o.name end end
    ED.L.objects, ED.L.joints = s.objects, s.joints
    for _, o in ipairs(ED.L.objects) do
        if is_ground(o) then o.name = ground[o.def .. o.x .. o.y] or (create(o) and o.name) else create(o) end
    end
    ED.selected = nil
    local first = LM.birdOrder(ED.L)[1]
    if first then ED.L.sling = {first.x * 20, first.y * 20} end
    ED.placeSling()
    changed()
end

function ED.doUndo()
    local s = table.remove(ED.undo)
    if not s then return end
    ED.redo[#ED.redo + 1] = snapshot()
    restore(s)
end
function ED.doRedo()
    local s = table.remove(ED.redo)
    if not s then return end
    ED.undo[#ED.undo + 1] = snapshot()
    restore(s)
end

local function index_of(o)
    for i, x in ipairs(ED.L.objects) do if x == o then return i end end
end

function ED.delete(o)
    local i = index_of(o)
    if not i or is_ground(o) then return end
    ED.remember()
    if o.name then removeObject(o.name) end
    table.remove(ED.L.objects, i)
    local joints = {}
    for _, j in ipairs(ED.L.joints) do
        if j.a ~= i and j.b ~= i then
            if j.a > i then j.a = j.a - 1 end
            if j.b > i then j.b = j.b - 1 end
            joints[#joints + 1] = j
        end
    end
    ED.L.joints = joints
    if ED.selected == o then ED.selected = nil end
    if LM.isBird(o.def) then ED.birdsChanged() end
    changed()
end

local DEFAULT_Z = {piglettes = 4, birds = 4, woodBlocks = 2, lightBlocks = 2, rockBlocks = 2, extras = 2,
                   staticBlocks = 0, staticFragile = 0, staticIndestructible = 0, decorations = 1, nocollision = -2}

function ED.add(def, x, y)
    local d = blockTable.blocks[def]
    local o = {def = def, x = x, y = y, angle = 0, z = d.z_order or DEFAULT_Z[d.group] or 2, extra = {}}
    if not create(o) then return nil end
    ED.L.objects[#ED.L.objects + 1] = o
    if LM.isBird(def) then ED.birdsChanged() end
    changed()
    return o
end

-- Birds moved, added or removed. The slingshot keeps its place and bird 1 stands at
-- it; dragging bird 1 takes the slingshot along. The others wait left of it in their
-- order, a new bird last.
function ED.birdsChanged(moved)
    local L = ED.L
    local birds = LM.birdOrder(L)
    for i, b in ipairs(birds) do
        b.extra = b.extra or {}
        b.extra.startNumber = i
    end
    local first = birds[1]
    if not first then return ED.placeSling() end
    if first == moved then
        L.sling = {first.x * 20, first.y * 20}
    else
        local sling = LM.slingPos(L)
        place(first, sling[1] / 20, sling[2] / 20 - 3)
        drop(first)
        L.sling = {first.x * 20, first.y * 20}
    end
    local fs = shape(first)
    local edge = first.x - (fs and fs.ex or 0.5) - 0.4
    for i = 2, #birds do
        local b = birds[i]
        local bs = shape(b)
        local ex = bs and bs.ex or 0.5
        if b.x + ex > edge then
            place(b, edge - ex, math.min(b.y, first.y) - 3)
            drop(b)
        end
        edge = math.min(edge, b.x - ex - 0.4)
    end
    ED.placeSling()
end
function ED.placeSling()
    local b = LM.birdOrder(ED.L)[1]
    local w = b and b.name and objects.world[b.name]
    if w then pcall(function() SlingshotSystem:instance():setPositionByStartObject(w) end) end
end

function ED.rotate(o, by)
    ED.remember()
    local x, y = o.x, o.y
    o.angle = (o.angle + by) % (2 * math.pi)
    if o.name then setRotation(o.name, o.angle) end
    settle(o, nil)
    if collides(o) then place(o, x, y) end
    changed()
end

function ED.copy(o)
    ED.remember()
    local s = shape(o)
    local c = ED.add(o.def, o.x + (s and s.ex * 2 + 0.05 or 1), o.y)
    if not c then return end
    c.angle, c.z, c.extra = o.angle, o.z, LM.copy(o.extra)
    if c.name then setRotation(c.name, c.angle) end
    if not settle(c, nil) then ED.delete(c) table.remove(ED.undo) return end
    ED.selected = c
end

function ED.layer(o, by)
    ED.remember()
    o.z = o.z + by
    if o.name then removeObject(o.name) create(o) end
    changed()
end

-- Connections (joints): glue, hinge (at the second element's centre), rope.
ED.JOINTS = {glue = 2, hinge = 3, rope = 5}
function ED.connect(a, b, kind)
    local ia, ib = index_of(a), index_of(b)
    if not ia or not ib or ia == ib then return end
    ED.remember()
    local j = {type = ED.JOINTS[kind], a = ia, b = ib, coords = 2, p = {0, 0, 0, 0}, extra = {}}
    if kind == 'hinge' then
        local c, s = math.cos(-a.angle), math.sin(-a.angle)
        local dx, dy = b.x - a.x, b.y - a.y
        j.p = {dx * c - dy * s, dx * s + dy * c, 0, 0}
    end
    ED.L.joints[#ED.L.joints + 1] = j
    changed()
end
function ED.disconnect(o)
    local i = index_of(o)
    ED.remember()
    local joints = {}
    for _, j in ipairs(ED.L.joints) do if j.a ~= i and j.b ~= i then joints[#joints + 1] = j end end
    ED.L.joints = joints
    changed()
end

-- ---- picking
local function world_at(x, y) return screenToPhysicsTransform(x, y) end
local function pick(x, y)
    local px, py = world_at(x, y)
    local best, bz
    for _, o in ipairs(ED.L.objects) do
        if not is_ground(o) then
            local s = shape(o)
            if s then
                local c, n = math.cos(-s.angle), math.sin(-s.angle)
                local dx, dy = px - s.x, py - s.y
                local lx, ly = dx * c - dy * n, dx * n + dy * c
                local pad = 8 * unit()
                if math.abs(lx) <= s.hw + pad and math.abs(ly) <= s.hh + pad and (not bz or o.z >= bz) then best, bz = o, o.z end
            end
        end
    end
    return best
end

-- ---- camera
local function camera_handler()
    local h = {}
    function h:update() return {x = ED.cam.x, y = ED.cam.y, zoom = ED.cam.zoom, movementDuration = 0, zoomDuration = 0}, true end
    return h
end
function ED.zoom(f, sx, sy)
    local cam = ED.cam
    sx, sy = sx or screenWidth * 0.5, sy or screenHeight * 0.5
    local nz = math.max(0.15, math.min(2.5, cam.zoom * f))
    -- keep the point under (sx, sy) in place
    local ox, oy = sx - screenWidth * 0.5, sy - screenHeight * 0.5
    cam.x = cam.x + ox / cam.zoom - ox / nz
    cam.y = cam.y + oy / cam.zoom - oy / nz
    cam.zoom = nz
end

-- ---- entering and leaving
function LM.enterEditor()
    local L = LM.level
    ED.L = L
    for i, o in ipairs(L.objects) do o.name = LM.objectName(o, i) if not objects.world[o.name] then o.name = nil end end
    -- back from a test or a new background: the same level, its undo steps kept
    local keep = ED.keep and ED.keep.level == L and ED.keep
    ED.undo, ED.redo = keep and keep.undo or {}, keep and keep.redo or {}
    ED.selected, ED.category, ED.menu, ED.drag, ED.connecting = nil, nil, nil, nil, nil
    ED.trayX = 0
    ED.hidden = {}
    ED.dirty = ED.dirty or false
    setPhysicsEnabled(false)
    -- the birds numbered, bird 1 at the slingshot
    local numbered = true
    for _, b in ipairs(LM.birdOrder(L)) do if not (b.extra and b.extra.startNumber) then numbered = false end end
    if not numbered then ED.birdsChanged() end
    build_palette()
    ED.screen = UI.Screen:new({name = 'levelMakerEditor', build = ED.build})
    ED.hud = nil
    ED.attach()
    UI.onBack = function() ED.exitPrompt() end
    UI.onWheel = function(n, x, y)
        -- over the elements' strip: it scrolls
        local sy = y / UI.k()
        if ED.category and ED.trayScroll and sy > H - 210 and sy < H - 92 then return ED.trayScroll(n > 0 and -104 or 104) end
        ED.zoom(n > 0 and 1.15 or 1 / 1.15, x, y)
    end
    local cam = ED.savedCam
    if not cam then
        -- the whole level in view
        local sling = LM.slingPos(L)
        local l, t, r, b = LM.bounds(L)
        l, r = math.min(l or sling[1] + 800, sling[1] - 150), math.max(r or sling[1] + 1400, sling[1] + 900)
        t, b = math.min(t or -300, sling[2] - 300), math.max(b or 100, sling[2] + 100)
        local zoom = math.min(screenWidth / (r - l + 200), screenHeight / (b - t + 500))
        cam = {x = (l + r) * 0.5, y = (t + b) * 0.5 + 40 / zoom, zoom = math.max(0.15, math.min(zoom, 1.2))}
    end
    ED.cam = cam
    ED.oldHandler = cameraSystem.cameraInputHandler
    cameraSystem:unlockCameraLimits()
    cameraSystem:setInputHandler(camera_handler())
    setGameMode(ED.update)
    LM.editing = true
    setPhysicsEnabled(false)
    LM.host('editor', '1')
end

-- The editor's screen on the menu root (the game replaces the HUD after loading),
-- the HUD's own parts hidden.
function ED.attach()
    local root = menuManager.menus and menuManager.menus.root
    if not root or not ED.screen or not LM.editing then return end
    if ED.hud ~= root then
        if ED.hud then ED.hud:removeChild(ED.screen) rawset(ED.hud, 'draw', nil) end
        ED.hud = root
        root:addChild(ED.screen)
        -- only the children: no score
        rawset(root, 'draw', function(self, ...) return ui.Frame.draw(self, ...) end)
    end
    for _, c in ipairs(root.children or {}) do
        if c ~= ED.screen and c.visible ~= false then
            ED.hidden[#ED.hidden + 1] = c
            c:setVisible(false)
        end
    end
end

local function leave_scene()
    LM.editing = false
    setGameMode(function() drawGame() end)
    LM.host('editor', '0')
    ED.savedCam = ED.cam
    if ED.screen and ED.hud then ED.hud:removeChild(ED.screen) rawset(ED.hud, 'draw', nil) end
    for _, c in ipairs(ED.hidden or {}) do c:setVisible(true) end
    ED.screen = nil
    UI.onBack, UI.onWheel = nil, nil
    if ED.oldHandler then cameraSystem:setInputHandler(ED.oldHandler) end
end

-- The level without the editor's names.
local function clean(L)
    local c = LM.copy(L)
    for _, o in ipairs(c.objects) do o.name = nil end
    return c
end

-- Saves into the level's slot, or a new one.
function ED.save()
    local L = clean(ED.L)
    local code, err = LM.encode(L)
    if not code then ED.toast(T('failed') .. ' (' .. tostring(err) .. ')') return false end
    local slots = LM.store().slots
    local slot = LM.slot and slots[LM.slot]
    if not slot then
        if #slots >= LM.MAX_SLOTS then ED.toast(T('slots_full')) return false end
        slot = {best = 0, stars = 0}
        slots[#slots + 1] = slot
        LM.slot = #slots
    end
    if slot.code ~= code then slot.best, slot.stars = 0, 0 end
    slot.code = code
    slot.beaten = LM.editorBeaten == code and code or nil
    LM.code = code
    LM.saveStore()
    ED.dirty = false
    ED.toast(T('saved'))
    return true
end

function ED.test()
    if not ED.hasBirdsAndPigs() then ED.toast(T('need_birds_pigs')) return end
    local L = clean(ED.L)
    ED.keep = {level = L, undo = ED.undo, redo = ED.redo}
    leave_scene()
    LM.later = function() LM.start(L, 'test') end
end

function ED.hasBirdsAndPigs()
    local pigs, birds = false, false
    for _, o in ipairs(ED.L.objects) do
        if LM.isPig(o.def) then pigs = true elseif LM.isBird(o.def) then birds = true end
    end
    return pigs and birds
end

-- Won in a test: the stars are set from the score, and the level counts as beaten.
function LM.editorWin(score)
    local L = LM.level
    L.stars = {math.floor(score * 0.6 / 100) * 100, math.floor(score * 0.85 / 100) * 100}
    LM.editorBeaten = LM.encode(L)
    ED.justBeaten = true
end

-- Rebuilds the scene (a new theme), keeping the editor's state.
function ED.rebuild()
    local L = clean(ED.L)
    ED.keep = {level = L, undo = ED.undo, redo = ED.redo}
    leave_scene()
    LM.later = function() LM.start(L, 'edit') end
end

function ED.exit()
    local L = clean(ED.L)
    leave_scene()
    ED.savedCam = nil
    LM.level = L
    LM.mode = nil
    LM.reopen = LM.slot and true or 'build'
    if not LM.slot then LM.code = nil end
    LM.later = function() eventManager:notify({id = events.EID_CHANGE_SCENE, target = 'MAIN_MENU'}) end
end

function ED.exitPrompt()
    if ED.menu then ED.menu = nil return end
    if not ED.dirty then return ED.exit() end
    ED.menu = {kind = 'exit'}
end

function ED.toast(text) ED.toastText, ED.toastTime = text, 120 end

-- ---- the frame: camera, input from the host, the scene
function ED.update(dt)
    ED.attach()
    -- the level's music, which the game plays only while the level runs
    local music = LM.env and LM.env.currentMusic
    if music and not res.isAudioPlaying(music) then LM.env.playAudio(music) end
    local events = LM.host('events').list or {}
    for _, e in ipairs(events) do ED.onHostEvent(e) end
    local touches = menuManager.getFilteredTouches and menuManager:getFilteredTouches() or nil
    local ids = {}
    for id, t in pairs(touches or {}) do ids[#ids + 1] = t end
    if #ids >= 2 then
        local a, b = ids[1], ids[2]
        local d = math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2)
        local mx, my = (a.x + b.x) * 0.5, (a.y + b.y) * 0.5
        if ED.pinch and ED.pinch.d > 0 then
            ED.zoom(d / ED.pinch.d, mx, my)
            ED.cam.x = ED.cam.x - (mx - ED.pinch.x) / ED.cam.zoom
            ED.cam.y = ED.cam.y - (my - ED.pinch.y) / ED.cam.zoom
        end
        ED.pinch = {d = d, x = mx, y = my}
        if ED.drag and ED.drag.kind == 'object' then ED.cancelDrag() end
        ED.drag = nil
    else
        ED.pinch = nil
    end
    cameraSystem:updateCamera(dt)
    G.CameraNative.updateGFXEffects(dt)
    drawGame()
end

local KEYS = {}
function ED.onHostEvent(e)
    local kind = e[1]
    if kind == 'secondary' then
        local x, y = e[2] or cursor.x, e[3] or cursor.y
        if not UI.secondary(x, y) then
            local o = pick(x, y)
            if o then ED.selected = o ED.menu = {kind = 'object', o = o} end
        end
    elseif kind == 'wheel' then
        if UI.onWheel then UI.onWheel(e[2], e[3], e[4]) end
    elseif kind == 'key' then
        local f = KEYS[e[2]]
        if f then f() end
    end
end
KEYS.DELETE = function() if ED.selected then ED.delete(ED.selected) end end
KEYS.ROTATE = function() if ED.selected then ED.rotate(ED.selected, math.pi * 0.5) end end
KEYS.ROTATE_LEFT = function() if ED.selected then ED.rotate(ED.selected, -math.pi / 12) end end
KEYS.ROTATE_RIGHT = function() if ED.selected then ED.rotate(ED.selected, math.pi / 12) end end
KEYS.UNDO = function() ED.doUndo() end
KEYS.REDO = function() ED.doRedo() end
KEYS.COPY = function() if ED.selected then ED.copy(ED.selected) end end
KEYS.TEST = function() ED.test() end
KEYS.SAVE = function() ED.save() end
KEYS.ZOOM_IN = function() ED.zoom(1.15) end
KEYS.ZOOM_OUT = function() ED.zoom(1 / 1.15) end
KEYS.LEFT = function() ED.cam.x = ED.cam.x - 60 / ED.cam.zoom end
KEYS.RIGHT = function() ED.cam.x = ED.cam.x + 60 / ED.cam.zoom end
KEYS.UP = function() ED.cam.y = ED.cam.y - 60 / ED.cam.zoom end
KEYS.DOWN = function() ED.cam.y = ED.cam.y + 60 / ED.cam.zoom end

-- ---- dragging in the scene
function ED.cancelDrag()
    local d = ED.drag
    if d and d.kind == 'object' then
        if d.new then ED.delete(d.o) table.remove(ED.undo) else place(d.o, d.fromx, d.fromy) end
    end
    ED.drag = nil
end

local function over_bar(y) return y > (H - (ED.category and (#ED.category.tabs > 1 and 262 or 210) or 100)) * UI.k() end

local function drag_object(o, x, y, new)
    local px, py = world_at(x, y)
    ED.drag = {kind = 'object', o = o, dx = o.x - px, dy = o.y - py, fromx = o.x, fromy = o.y, new = new}
    if not new then ED.remember() end
end

local function scene_drag(phase, x, y)
    if phase == 'start' then
        if ED.menu then return end
        local o = pick(x, y)
        if o and o == ED.selected then
            drag_object(o, x, y, false)
        else
            ED.drag = {kind = 'pan', x = x, y = y, o = o}
        end
    elseif phase == 'move' then
        local d = ED.drag
        if not d then return end
        if d.kind == 'pan' then
            if d.o and math.abs(x - d.x) + math.abs(y - d.y) > 14 * UI.k() and not d.panning then
                -- dragging an element that was not selected: select and move it
                ED.selected = d.o
                drag_object(d.o, d.x, d.y, false)
                return scene_drag('move', x, y)
            end
            d.panning = true
            ED.cam.x = ED.cam.x - (x - d.x) / ED.cam.zoom
            ED.cam.y = ED.cam.y - (y - d.y) / ED.cam.zoom
            d.x, d.y = x, y
        elseif d.kind == 'object' then
            local px, py = world_at(x, y)
            local nx, ny = snap(d.o, px + d.dx, py + d.dy)
            place(d.o, nx, ny)
            d.bad = collides(d.o) ~= nil
            d.trash = over_bar(y)
        end
    elseif phase == 'end' then
        local d = ED.drag
        ED.drag = nil
        if not d then return end
        if d.kind == 'object' then
            if d.trash then
                if d.new then ED.delete(d.o) table.remove(ED.undo) else table.remove(ED.undo) ED.delete(d.o) end
                return
            end
            if not settle(d.o, not d.new and d.fromx or nil, d.fromy) then
                ED.delete(d.o)
                table.remove(ED.undo)
                ED.toast(T('no_room'))
                return
            end
            if in_sling(d.o) then
                if d.new then ED.delete(d.o) table.remove(ED.undo) else place(d.o, d.fromx, d.fromy) table.remove(ED.undo) end
                ED.toast(T('no_room'))
                return
            end
            ED.selected = d.o
            if LM.isBird(d.o.def) then ED.birdsChanged(d.o) end
            changed()
        end
    end
end

local function scene_tap(x, y)
    -- the settings close with their X (or the back key) only
    if ED.menu then
        if ED.menu.kind ~= 'settings' then ED.menu = nil end
        return
    end
    local o = pick(x, y)
    if ED.connecting then
        if o and o ~= ED.connecting.o then ED.connect(ED.connecting.o, o, ED.connecting.kind) end
        ED.connecting = nil
        return
    end
    ED.selected = o
end

local function scene_long(x, y)
    local o = pick(x, y)
    if o then ED.selected = o ED.menu = {kind = 'object', o = o} end
end


-- ---- drawing
local function outline(o, r, g, b)
    local s = shape(o)
    if not s then return end
    local k = UI.k()
    local pts = corners(s)
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    for i = 1, 4 do
        local p, q = pts[i], pts[i % 4 + 1]
        local x1, y1 = physicsToScreenTransform(p[1], p[2])
        local x2, y2 = physicsToScreenTransform(q[1], q[2])
        drawLine2D(x1, y1, x2, y2, 3 * k, r, g, b, 255)
    end
end

local function draw_scene_marks()
    local k = UI.k()
    -- connections
    for _, j in ipairs(ED.L.joints) do
        local a, b = ED.L.objects[j.a], ED.L.objects[j.b]
        if a and b then
            local x1, y1 = physicsToScreenTransform(a.x, a.y)
            local x2, y2 = physicsToScreenTransform(b.x, b.y)
            local c = j.type == 3 and {255, 200, 0} or (j.type == 5 and {200, 120, 40} or {120, 220, 255})
            setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
            drawLine2D(x1, y1, x2, y2, 3 * k, c[1], c[2], c[3], 220)
        end
    end
    -- the birds' order
    for i, o in ipairs(LM.birdOrder(ED.L)) do
        local x, y = physicsToScreenTransform(o.x, o.y)
        UI.label(tostring(i), x / k, y / k - 40, 26, 'FONT_HEADER')
    end
    if ED.selected then
        local d = ED.drag
        if d and d.o == ED.selected and (d.bad or d.trash) then outline(ED.selected, 255, 60, 40) else outline(ED.selected, 255, 220, 40) end
    end
    if ED.connecting then outline(ED.connecting.o, 120, 220, 255) end
end

local function tray(c)
    local w = W()
    local y0 = H - 210
    local items = c.tab and c.tab.items or {}
    if #c.tabs > 1 then
        local tw = math.min(220, (w - 20) / #c.tabs)
        UI.panel(-20, y0 - 52, w + 40, 60, 'dark')
        for i, tab in ipairs(c.tabs) do
            UI.button(10 + (i - 1) * tw, y0 - 48, tw - 6, 44, T('grp_' .. tab.group), {id = 'tab' .. tab.group, size = 22,
                selected = c.tab == tab, tap = function() c.tab = tab ED.trayX = 0 end})
        end
    end
    UI.panel(-20, y0, w + 40, 118, 'dark')
    -- a strip that scrolls (dragged sideways, the wheel, the arrows a strip's width at a time)
    local size, gap = 96, 8
    local left, right = 70, w - 70
    local view = right - left
    local most = math.max(0, #items * (size + gap) - gap - view)
    ED.trayX = math.max(0, math.min(ED.trayX or 0, most))
    local function scroll(by) ED.trayX = math.max(0, math.min(ED.trayX + by, most)) end
    ED.trayScroll = function(by) scroll(by) end
    UI.button(4, y0 + 25, 60, 60, nil, {id = 'trayprev', icon = 'BUTTON_ARROW_LEFT', enabled = ED.trayX > 0, tap = function() scroll(-view) end})
    UI.button(w - 64, y0 + 25, 60, 60, nil, {id = 'traynext', icon = 'BUTTON_ARROW_RIGHT', enabled = ED.trayX < most, tap = function() scroll(view) end})
    local k = UI.k()
    -- dragged sideways: the strip moves; upwards out of it: the element goes into the level
    local function strip_drag(def)
        return function(phase, x, y)
            if phase == 'start' then ED.trayDrag = {x = x, y = y, from = ED.trayX} return end
            local t = ED.trayDrag
            if not t then return end
            if not ED.drag and not t.sideways and math.abs(x - t.x) > 12 * k and math.abs(x - t.x) > math.abs(y - t.y) then t.sideways = true end
            if t.sideways then
                ED.trayX = math.max(0, math.min(t.from - (x - t.x) / k, most))
                if phase == 'end' then ED.trayDrag = nil end
                return
            end
            if def and phase == 'move' and not ED.drag and y < (y0 - 60) * k then
                local px, py = world_at(x, y)
                ED.remember()
                local o = ED.add(def, px, py)
                if not o then table.remove(ED.undo) return end
                ED.selected = o
                drag_object(o, x, y, true)
            end
            if ED.drag then scene_drag(phase, x, y) end
            if phase == 'end' then ED.trayDrag = nil end
        end
    end
    UI.area(left, y0, view, 118, {id = 'strip', drag = strip_drag(nil)})
    local cx, cy, cw, ch = res.getClipRect()
    res.setClipRect(math.floor(left * k), math.floor(y0 * k), math.ceil(view * k), math.ceil(118 * k))
    local first = math.max(1, math.floor(ED.trayX / (size + gap)) + 1)
    for i = first, #items do
        local def = items[i]
        local x = left + (i - 1) * (size + gap) - ED.trayX
        if x >= right then break end
        UI.panel(x, y0 + 7, size, 96, 'dark')
        UI.icon(blockTable.blocks[def].sprite, x + size * 0.5, y0 + 55, size - 12, 84)
        -- the visible part only
        local ax = math.max(x, left)
        local aw = math.min(x + size, right) - ax
        if aw > 0 then
            UI.area(ax, y0 + 7, aw, 96, {id = 'item' .. def,
                tap = function()
                    local px, py = world_at(screenWidth * 0.5, screenHeight * 0.3)
                    ED.remember()
                    local o = ED.add(def, px, py)
                    if not o then table.remove(ED.undo) return end
                    drop(o)
                    if not settle(o, nil) or in_sling(o) then ED.delete(o) table.remove(ED.undo) ED.toast(T('no_room')) return end
                    if LM.isBird(def) then ED.birdsChanged() end
                    ED.selected = o
                end,
                drag = strip_drag(def)})
        end
    end
    res.setClipRect(cx or 0, cy or 0, cw or screenWidth, ch or screenHeight)
end

local function object_menu(m)
    local o = m.o
    local w = W()
    local items = {
        {T('rotate_left'), function() ED.rotate(o, -math.pi / 12) end, true, 'BUTTON_ARROW_LEFT'},
        {T('rotate_right'), function() ED.rotate(o, math.pi / 12) end, true, 'BUTTON_ARROW_RIGHT'},
        {T('to_front'), function() ED.layer(o, 1) end},
        {T('to_back'), function() ED.layer(o, -1) end},
        {T('glue'), function() ED.connecting = {o = o, kind = 'glue'} ED.toast(T('tap_other')) end},
        {T('hinge'), function() ED.connecting = {o = o, kind = 'hinge'} ED.toast(T('tap_other')) end},
        {T('rope'), function() ED.connecting = {o = o, kind = 'rope'} ED.toast(T('tap_other')) end},
        {T('disconnect'), function() ED.disconnect(o) end, nil, 'MENU_NO'},
    }
    local bw, bh = 260, 64
    local x0 = w * 0.5 - bw - 10
    UI.panel(x0 - 20, 110, bw * 2 + 60, 4 * (bh + 12) + 40, 'popup')
    for i, it in ipairs(items) do
        local col, row = (i - 1) % 2, math.floor((i - 1) / 2)
        UI.button(x0 + col * (bw + 20), 130 + row * (bh + 12), bw, bh, it[1], {id = 'm' .. i, icon = it[4], glyph = it[5], size = 26, tap = function()
            if not it[3] then ED.menu = nil end
            it[2]()
        end})
    end
end

ED.POWERUP_ICONS = {
    kingsling = 'INGAME_POWERUP_SLING', slingscope = 'INGAME_POWERUP_SCOPE', powerpotion = 'INGAME_POWERUP_SEED',
    birdquake = 'INGAME_POWERUP_EARTHQ', shockwave = 'INGAME_POWERUPS_SHOCKWAVE', redsmightyfeathers = 'PARTICLE_REDS_FEATHER',
}

local function settings_menu()
    local w = W()
    local L = ED.L
    UI.box(0, 0, w, H, C.dim)
    UI.panel(60, 40, w - 120, H - 80, 'popup')
    UI.label(T('settings'), w * 0.5, 80, 40, 'FONT_HEADER')
    UI.button(w - 140, 54, 64, 64, nil, {id = 'close', icon = 'MENU_NO', tap = function() ED.menu = nil end})
    local x, y = 100, 125
    -- a row choosing one of n with arrows that wrap around
    local function chooser(id, label, n, i, name, pick)
        UI.label(label, x, y + 30, 28, 'FONT_BASIC', 'LEFT')
        UI.button(x + 260, y, 70, 60, nil, {id = id .. 'prev', icon = 'BUTTON_ARROW_LEFT', tap = function() pick((i - 2) % n + 1) end})
        UI.label(name, x + 460, y + 30, 28)
        UI.button(x + 590, y, 70, 60, nil, {id = id .. 'next', icon = 'BUTTON_ARROW_RIGHT', tap = function() pick(i % n + 1) end})
    end
    -- theme
    local themes = ED.themes()
    local shown = ED.menu.theme or L.theme
    local ti = 1
    for i, t in ipairs(themes) do if t == shown then ti = i end end
    chooser('th', T('background'), #themes, ti, ED.themeName(shown), function(i) ED.menu.theme = themes[i] end)
    if ED.menu.theme and ED.menu.theme ~= L.theme then
        UI.button(x + 680, y, 200, 60, T('apply'), {id = 'thapply', icon = 'CHECKMARK', color = C.play, tap = function()
            ED.remember()
            -- the level and the view to where the new background's levels stand
            local dy = LM.changeTheme(L, ED.menu.theme)
            ED.cam.y = ED.cam.y + dy * 20
            ED.menu = nil
            changed()
            ED.rebuild()
        end})
    end
    -- the gravity
    y = y + 75
    UI.label(T('gravity'), x, y + 30, 28, 'FONT_BASIC', 'LEFT')
    for i, g in ipairs(LM.GRAVITY) do
        UI.button(x + 260 + (i - 1) * 210, y, 200, 60, T('gravity' .. i), {id = 'g' .. i, selected = (L.gravity or 20) == g,
            tap = function() if (L.gravity or 20) ~= g then ED.remember() L.gravity = g changed() end end})
    end
    -- weather
    y = y + 75
    UI.label(T('weather'), x, y + 30, 28, 'FONT_BASIC', 'LEFT')
    local weather = L.particles and L.particles.name
    local kinds = {{'wn', 'weather_none'}, {'wr', 'weather_rain', 'theme15rain'}, {'ws', 'weather_snow', 'lmSnow'},
                   {'wc', 'weather_confetti', 'lmConfetti'}}
    for i, k in ipairs(kinds) do
        UI.button(x + 260 + (i - 1) * 210, y, 200, 60, T(k[2]), {id = k[1], selected = weather == k[3], tap = function()
            if weather == k[3] then return end
            ED.remember()
            if k[3] then
                local l, t, r = LM.bounds(L)
                l, t, r = l or 0, t or -400, r or 1600
                L.particles = {x = (l + r) * 0.5, y = t - 1200, width = (r - l) + 4000, name = k[3], start = 0, frame = 0}
            else
                L.particles = nil
            end
            changed()
        end})
    end
    -- music: none, the background's, or one of LM.MUSIC; it plays at once
    y = y + 75
    local music = {'off', false}
    for _, m in ipairs(LM.MUSIC) do music[#music + 1] = m end
    local mi = 2
    for i, m in ipairs(music) do if m == (L.music or false) then mi = i end end
    local mname = mi == 1 and T('music_off') or mi == 2 and T('music_default') or T('music_' .. music[mi])
    chooser('mu', T('music'), #music, mi, mname, function(i)
        ED.remember()
        L.music = music[i] or nil
        changed()
        LM.playMusic(L)
    end)
    -- power-ups: how often each can be used, 0 to LM.MAX_USES
    y = y + 75
    UI.label(T('powerups'), x, y + 30, 28, 'FONT_BASIC', 'LEFT')
    UI.label(T('powerups_hint'), x + 260, y + 30, 24, 'FONT_BASIC', 'LEFT')
    L.limits = L.limits or {}
    local cw = (w - 200) / 3
    for i, p in ipairs(LM.POWERUPS) do
        local cx, cy = x + ((i - 1) % 3) * cw, y + 62 + math.floor((i - 1) / 3) * 66
        local n = L.limits[i] or 0
        local function set(v) if v ~= n then ED.remember() L.limits[i] = v changed() end end
        UI.button(cx, cy, 52, 58, nil, {id = 'pum' .. i, icon = 'BUTTON_ARROW_LEFT', tap = function() set((n - 1) % (LM.MAX_USES + 1)) end})
        UI.button(cx + 56, cy, cw - 130, 58, T('pu_' .. p) .. '  ' .. n, {id = 'pu' .. i, icon = ED.POWERUP_ICONS[p], size = 22,
            color = n > 0 and C.play or C.off, tap = function() set((n + 1) % (LM.MAX_USES + 1)) end})
        UI.button(cx + cw - 70, cy, 52, 58, nil, {id = 'pup' .. i, icon = 'BUTTON_ARROW_RIGHT', tap = function() set((n + 1) % (LM.MAX_USES + 1)) end})
    end
end

function ED.themes()
    if not ED.themeList then
        local list = {}
        for name, t in pairs(blockTable.themes) do
            -- not 'settings', and not the menus' (themeUGC_menu)
            if name ~= 'settings' and not string.find(name, '_menu$') and type(t) == 'table' then list[#list + 1] = name end
        end
        table.sort(list, function(a, b)
            local na, nb = tonumber(string.match(a, '^theme(%d+)$')), tonumber(string.match(b, '^theme(%d+)$'))
            if na and nb then return na < nb end
            if na or nb then return na ~= nil end
            return a < b
        end)
        ED.themeList = list
    end
    return ED.themeList
end
function ED.themeName(t)
    local n = string.match(t, '^theme(%d+)$')
    if n then return T('background') .. ' ' .. n end
    return string.gsub(t, '^theme', '')
end

local function exit_menu()
    local w = W()
    UI.box(0, 0, w, H, C.dim)
    UI.panel(w * 0.5 - 380, 180, 760, 350, 'popup')
    UI.label(T('save_changes'), w * 0.5, 245, 40, 'FONT_HEADER', 'HCENTER', 'VCENTER', 700)
    UI.choice(w * 0.5 - 220, 370, 120, 'MENU_YES', T('save'), {id = 'xs', tap = function() if ED.save() then ED.exit() end end})
    UI.choice(w * 0.5, 370, 120, 'MENU_NO', T('discard'), {id = 'xd', tap = function() ED.exit() end})
    UI.choice(w * 0.5 + 220, 370, 120, 'GESTURE_PLAY', T('cancel'), {id = 'xc', tap = function() ED.menu = nil end})
end

-- The copy button's picture: the element twice, one behind the other.
local function copy_icon(o, x, y)
    local d = blockTable.blocks[o.def]
    if not d or not d.sprite then return UI.label('x2', x, y, 30, '=FONT_BASIC') end
    UI.sprite(d.sprite, x - 7, y - 7, 34, 34, 0.55)
    UI.sprite(d.sprite, x + 6, y + 6, 34, 34)
end
-- A white dot with a brown edge, as the game's button icons are drawn.
local function dot(x, y)
    UI.round(x - 7, y - 7, 14, 14, {0.45, 0.22, 0.05}, 1, 7)
    UI.round(x - 5, y - 5, 10, 10, {1, 1, 1}, 1, 5)
end

function ED.build()
    local w = W()
    local k = UI.k()
    draw_scene_marks()
    -- the scene itself: everything not covered by buttons
    UI.area(0, 0, w, H, {id = 'scene', tap = scene_tap, long = scene_long, drag = scene_drag})
    -- top bar
    UI.button(14, 14, 52, 52, nil, {id = 'exit', icon = 'BUTTON_MENU', tip = T('exit'), tap = ED.exitPrompt})
    UI.button(84, 14, 52, 52, nil, {id = 'undo', icon = 'BUTTON_RESTART', mirrored = true, tip = T('undo'), enabled = #ED.undo > 0, tap = ED.doUndo})
    UI.button(146, 14, 52, 52, nil, {id = 'redo', icon = 'BUTTON_RESTART', tip = T('redo'), enabled = #ED.redo > 0, tap = ED.doRedo})
    UI.button(w - 96, 10, 84, 84, nil, {id = 'test', icon = 'GESTURE_PLAY', tip = T('test'), grey = not ED.hasBirdsAndPigs(), tap = ED.test})
    UI.button(w - 166, 14, 52, 52, nil, {id = 'settings', icon = 'BUTTON_OPTIONS', tip = T('settings'), tap = function() ED.menu = {kind = 'settings'} end})
    UI.button(w - 228, 14, 52, 52, nil, {id = 'save', icon = 'CHECKMARK', tip = T('save'), tap = ED.save})
    if LM.editorBeaten then UI.label(T('beaten'), w - 100, 110, 24) end
    -- the selection's buttons
    local s = ED.selected
    if s and not ED.drag then
        local bx = w - 16 - 4 * 100
        local by = H - (ED.category and (#ED.category.tabs > 1 and 262 or 210) or 100) - 90
        UI.button(bx, by, 76, 76, nil, {id = 'rot', glyph = '90°', tip = T('rotate'), tap = function() ED.rotate(s, math.pi * 0.5) end})
        UI.button(bx + 100, by, 76, 76, nil, {id = 'dup', icon = 'BUTTON_EMPTY', tip = T('copy_short'), tap = function() ED.copy(s) end})
        copy_icon(s, bx + 138, by + 36)
        -- MENU_NO fills its picture with the button: as large as the others
        UI.button(bx + 205, by + 5, 66, 66, nil, {id = 'del', icon = 'MENU_NO', tip = T('delete'), tap = function() ED.delete(s) end})
        UI.button(bx + 300, by, 76, 76, nil, {id = 'more', icon = 'BUTTON_EMPTY', tip = T('more'), tap = function()
            if ED.menu and ED.menu.kind == 'object' and ED.menu.o == s then ED.menu = nil else ED.menu = {kind = 'object', o = s} end
        end})
        for i = -1, 1 do dot(bx + 338 + i * 15, by + 36) end
    end
    -- categories
    local cats = ED.CATEGORIES
    local bw = (w - 32) / #cats
    UI.panel(-20, H - 100, w + 40, 120, 'dark')
    for i, c in ipairs(cats) do
        local x = 16 + (i - 1) * bw
        UI.button(x + 3, H - 94, bw - 6, 88, nil, {id = 'cat' .. c.id, selected = ED.category == c, tap = function()
            ED.category = ED.category ~= c and c or nil
            ED.trayX = 0
        end})
        -- inside the button's border
        if c.icon and blockTable.blocks[c.icon] then UI.icon(blockTable.blocks[c.icon].sprite, x + bw * 0.5, H - 63, bw - 44, 36) end
        UI.label(T('cat_' .. c.id), x + bw * 0.5, H - 31, 18, 'FONT_BASIC', 'HCENTER', 'VCENTER', bw - 32)
    end
    if ED.category then tray(ED.category) end
    if ED.drag and ED.drag.kind == 'object' and ED.drag.trash then
        UI.label(T('drop_delete'), w * 0.5, H - 240, 30)
    end
    -- menus
    local m = ED.menu
    if m then
        if m.kind == 'object' then object_menu(m)
        elseif m.kind == 'settings' then settings_menu()
        elseif m.kind == 'exit' then exit_menu() end
    end
    if ED.justBeaten then ED.justBeaten = nil ED.toast(T('beaten_now')) end
    if ED.toastText and ED.toastTime > 0 then
        ED.toastTime = ED.toastTime - 1
        UI.panel(w * 0.5 - 330, 120, 660, 60, 'popup')
        UI.label(ED.toastText, w * 0.5, 150, 28, 'FONT_BASIC', 'HCENTER', 'VCENTER', 640)
    end
end
)";

}  // namespace slingshot::level_maker
