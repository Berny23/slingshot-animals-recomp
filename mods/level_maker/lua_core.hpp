// SPDX-License-Identifier: GPL-3.0-or-later
// level-maker: levels as data, the saved levels, and playing them.
//
// A level (table) is
//   {theme, gravity, sling = {x, y}, stars = {two, three}, limits = {uses per power-up of LM.POWERUPS},
//    music = clip name or 'off' (none: the background's),
//    particles = {x, y, width, name, start, frame}, boom = {left, top, right, bottom},
//    objects = {{def, x, y, angle, z, extra = {key = value}}...},
//    joints = {{type, a, b, coords, p = {x1, y1, x2, y2}, extra = {...}}...}}
// with the meaning of codec.hpp's text lines, which LM.text and LM.parse convert to
// and from. LM.loaded turns it into what the game's level files hold
// (gamelua.loadedObjects).
//
// A custom level is played as level LevelMaker1 of a hidden episode LEVELMAKER: the
// game loads a real level file first (the engine needs one) and the table then
// replaces what it loaded. Its sprites are queued with the level's own.
#pragma once

namespace slingshot::level_maker {

constexpr const char* kCoreLua = R"(
local G = _G
local LM = G.levelMaker or {}
G.levelMaker = LM
LM.EPISODE, LM.LEVEL = 'LEVELMAKER', 'LevelMaker1'
LM.FOLDER = '/levels/levelmaker'
LM.FILE = LM.FOLDER .. '/' .. LM.LEVEL
LM.BASE = '/levels/1_PoachedEggs/Level1'
LM.MAX_SLOTS = 60
LM.POWERUPS = {'kingsling', 'slingscope', 'powerpotion', 'birdquake', 'shockwave', 'redsmightyfeathers'}
LM.GRAVITY = {20, 0, -20}
LM.MAX_USES = 10
-- Music a level can have besides none and the background's: the level ambiences
-- and four tunes.
LM.MUSIC = {
    'ambient_theme1', 'ambient_theme2', 'ambient_theme3', 'ambient_theme7', 'construction_theme1', 'ambient_cave',
    'ambience_waterfall', 'birthday_ambience', 'beach_ambience', 'rain_ambience', 'ambience_blackbird_01', 'dino_ambience',
    'funky_theme', 'ml_birdstorm', 'ml_birdday7song', 'title_theme',
}

-- ---- host
function LM.host(cmd, arg)
    send('cache/slingshot-level-maker/', cmd .. '\n' .. (arg or ''))
    local r = read('cache/slingshot-level-maker-reply')
    if type(r) ~= 'table' then r = {} end
    return r
end

local function finite(v) return type(v) == 'number' and v == v and v > -1e7 and v < 1e7 end
local function num(v) return string.format('%.9g', v) end
LM.finite = finite

function LM.copy(t)
    if type(t) ~= 'table' then return t end
    local c = {}
    for k, v in pairs(t) do c[k] = LM.copy(v) end
    return c
end

-- Where a theme's ground is (y of its Ground objects in the game's levels); 5 for the rest.
LM.GROUND = {themeDino = -14, themeDino2 = -10, theme29 = 3, themeFarm = 3}
function LM.groundOf(theme) return LM.GROUND[theme] or 5 end
-- Moves a level up or down (joint points are relative to their objects).
function LM.shift(L, dy)
    for _, o in ipairs(L.objects) do o.y = o.y + dy end
    if L.sling then L.sling[2] = L.sling[2] + dy * 20 end
    if L.particles then L.particles.y = L.particles.y + dy * 20 end
    if L.boom then L.boom[2], L.boom[4] = L.boom[2] + dy * 20, L.boom[4] + dy * 20 end
end
function LM.limit(L, name)
    for i, p in ipairs(LM.POWERUPS) do if p == name then return (L.limits or {})[i] or 0 end end
end
-- The clip a level plays: its own, the background's, or none.
function LM.musicOf(L)
    if L.music == 'off' then return nil end
    if L.music then return L.music end
    local t = blockTable.themes[L.theme]
    return t and t.music
end

-- ---- saved levels: settings.levelMaker = {version = 1, slots = {{code, beaten, best, stars}...}}
function LM.store()
    local s = settings
    if type(s.levelMaker) ~= 'table' then s.levelMaker = {version = 1, slots = {}} end
    if type(s.levelMaker.slots) ~= 'table' then s.levelMaker.slots = {} end
    return s.levelMaker
end
function LM.saveStore() saveLuaFileWrapper('settings.lua', 'settings', true) end

-- ---- text lines <-> level
local function value_text(v)
    if type(v) == 'boolean' then return v and 'true' or 'false' end
    if type(v) == 'table' then return v[1] .. ',' .. v[2] .. ',' .. v[3] .. ',' .. v[4] end
    return num(v)
end
local function extras_text(extra)
    local keys = {}
    for k in pairs(extra or {}) do keys[#keys + 1] = k end
    table.sort(keys)
    local s = ''
    for _, k in ipairs(keys) do s = s .. ' ' .. k .. '=' .. value_text(extra[k]) end
    return s
end

function LM.text(L)
    local out = {'theme ' .. L.theme}
    if L.gravity then out[#out + 1] = 'gravity ' .. num(L.gravity) end
    if L.sling then out[#out + 1] = 'sling ' .. num(L.sling[1]) .. ' ' .. num(L.sling[2]) end
    if L.stars then out[#out + 1] = 'stars ' .. string.format('%d %d', L.stars[1], L.stars[2]) end
    local limits = {}
    for i = 1, #LM.POWERUPS do limits[i] = string.format('%d', (L.limits or {})[i] or 0) end
    out[#out + 1] = 'limits ' .. table.concat(limits, ' ')
    if L.music then out[#out + 1] = 'music ' .. L.music end
    local p = L.particles
    if p then
        local line = 'particles ' .. num(p.x) .. ' ' .. num(p.y) .. ' ' .. num(p.width)
        if p.name then line = line .. ' ' .. p.name .. string.format(' %d %d', p.start or 0, p.frame or 0) end
        out[#out + 1] = line
    end
    if L.boom then out[#out + 1] = 'boom ' .. num(L.boom[1]) .. ' ' .. num(L.boom[2]) .. ' ' .. num(L.boom[3]) .. ' ' .. num(L.boom[4]) end
    for _, o in ipairs(L.objects) do
        out[#out + 1] = 'o ' .. o.def .. ' ' .. num(o.x) .. ' ' .. num(o.y) .. ' ' .. num(o.angle) .. ' ' .. num(o.z) .. extras_text(o.extra)
    end
    for _, j in ipairs(L.joints) do
        out[#out + 1] = string.format('j %d %d %d %d ', j.type, j.a, j.b, j.coords) .. num(j.p[1]) .. ' ' .. num(j.p[2]) .. ' ' ..
            num(j.p[3]) .. ' ' .. num(j.p[4]) .. extras_text(j.extra)
    end
    return table.concat(out, '\n') .. '\n'
end

local function parse_value(s)
    if s == 'true' then return true end
    if s == 'false' then return false end
    local a, b, c, d = string.match(s, '^(%d+),(%d+),(%d+),(%d+)$')
    if a then return {tonumber(a), tonumber(b), tonumber(c), tonumber(d)} end
    local v = tonumber(s)
    if finite(v) then return v end
end
local function parse_extras(t, from)
    local extra = {}
    for i = from, #t do
        local k, v = string.match(t[i], '^([%w_%.]+)=(.+)$')
        if not k then return nil end
        v = parse_value(v)
        if v == nil then return nil end
        extra[k] = v
    end
    return extra
end

-- A level from text lines, checked against the game's data; nil and a reason otherwise.
function LM.parse(text)
    local L = {theme = 'theme1', limits = {}, objects = {}, joints = {}}
    for line in string.gmatch(text or '', '[^\n]+') do
        local t = {}
        for w in string.gmatch(line, '%S+') do t[#t + 1] = w end
        local k, n = t[1], function(i) return tonumber(t[i]) end
        if k == 'theme' then L.theme = t[2]
        elseif k == 'gravity' then L.gravity = n(2)
        elseif k == 'sling' then L.sling = {n(2), n(3)}
        elseif k == 'stars' then L.stars = {n(2), n(3)}
        elseif k == 'limits' then L.limits = {} for i = 2, #t do L.limits[i - 1] = n(i) end
        elseif k == 'music' then L.music = t[2]
        elseif k == 'particles' then L.particles = {x = n(2), y = n(3), width = n(4), name = t[5], start = n(6), frame = n(7)}
        elseif k == 'boom' then L.boom = {n(2), n(3), n(4), n(5)}
        elseif k == 'o' then
            local extra = parse_extras(t, 7)
            if not extra then return nil, 'object' end
            L.objects[#L.objects + 1] = {def = t[2], x = n(3), y = n(4), angle = n(5), z = n(6), extra = extra}
        elseif k == 'j' then
            local extra = parse_extras(t, 10)
            if not extra then return nil, 'joint' end
            L.joints[#L.joints + 1] = {type = n(2), a = n(3), b = n(4), coords = n(5), p = {n(6), n(7), n(8), n(9)}, extra = extra}
        elseif k then
            return nil, 'line ' .. tostring(k)
        end
    end
    return LM.check(L)
end

function LM.check(L)
    local themes = blockTable.themes
    if type(L.theme) ~= 'string' or L.theme == 'settings' or type(themes[L.theme]) ~= 'table' then return nil, 'theme' end
    if L.gravity ~= nil and not finite(L.gravity) then return nil, 'gravity' end
    if L.sling and not (finite(L.sling[1]) and finite(L.sling[2])) then return nil, 'sling' end
    if L.stars and not (finite(L.stars[1]) and finite(L.stars[2])) then return nil, 'stars' end
    L.limits = L.limits or {}
    for i = 1, #LM.POWERUPS do
        local v = L.limits[i] or 0
        if not finite(v) or v < 0 or v > LM.MAX_USES or v % 1 ~= 0 then return nil, 'limits' end
        L.limits[i] = v
    end
    if L.music ~= nil and L.music ~= 'off' then
        local known = false
        for _, m in ipairs(LM.MUSIC) do if m == L.music then known = true end end
        if not known then return nil, 'music' end
    end
    local p = L.particles
    if p and not (finite(p.x) and finite(p.y) and finite(p.width)) then return nil, 'particles' end
    if p and p.name and not (finite(p.start) and finite(p.frame)) then return nil, 'particles' end
    if L.boom then for i = 1, 4 do if not finite(L.boom[i]) then return nil, 'boom' end end end
    local blocks = blockTable.blocks
    for _, o in ipairs(L.objects) do
        if type(o.def) ~= 'string' or type(blocks[o.def]) ~= 'table' then return nil, 'object ' .. tostring(o.def) end
        if not (finite(o.x) and finite(o.y) and finite(o.angle) and finite(o.z)) then return nil, 'object' end
    end
    local count = #L.objects
    for _, j in ipairs(L.joints) do
        if not (finite(j.type) and finite(j.coords) and finite(j.a) and finite(j.b)) then return nil, 'joint' end
        if j.a < 1 or j.b < 1 or j.a > count or j.b > count then return nil, 'joint' end
        for i = 1, 4 do if not finite(j.p[i]) then return nil, 'joint' end end
    end
    return L
end

-- Share code of a level; nil and a reason when it has none.
function LM.encode(L)
    local r = LM.host('encode', LM.text(L))
    return r.code, r.error
end
function LM.decode(code)
    local r = LM.host('decode', code)
    if not r.text then return nil, r.error end
    return LM.parse(r.text)
end

-- ---- level <-> the game's level data
local BIRD_GROUPS = {birds = true}
-- Birds to shoot; caged birds are targets (the game's levels leave them unnumbered).
function LM.isBird(def)
    local d = blockTable.blocks[def]
    return d ~= nil and BIRD_GROUPS[d.group] == true and not string.find(def, '_Cage$')
end
function LM.isPig(def) local d = blockTable.blocks[def] return d ~= nil and d.group == 'piglettes' end

local function defaults(d)
    local m = blockTable.materials[d.materialName or ''] or {}
    return {strength = d.strength or m.strength, density = d.density or m.density, defence = d.defence or m.defence}
end

local function camera(l, t, r, b)
    local function view(w, h)
        local sx = w / math.max(r - l, 1)
        return {left = l, right = r, top = t, bottom = b, px = (l + r) * 0.5, py = (t + b) * 0.5, sx = sx, sy = sx,
                screenWidth = w, screenHeight = h}
    end
    local c = view(1680, 1024)
    c.version = 0.02
    c.iphone = view(480, 320)
    c.ipad = view(1024, 768)
    return c
end

LM.DEFAULT_SLING = {-352.76, 1.74}

-- World pixels around the level's objects: left, top, right, bottom (birds and the ground left out).
function LM.bounds(L)
    local l, t, r, b
    for _, o in ipairs(L.objects) do
        local d = blockTable.blocks[o.def]
        if d and d.group ~= 'ground' and not LM.isBird(o.def) then
            l = math.min(l or o.x, o.x - 1.5)
            r = math.max(r or o.x, o.x + 1.5)
            t = math.min(t or o.y, o.y - 1.5)
            b = math.max(b or o.y, o.y + 1.5)
        end
    end
    if not l then return nil end
    return l * 20, t * 20, r * 20, b * 20
end

-- The birds in shooting order: by their numbers, the others from the right.
function LM.birdOrder(L)
    local birds = {}
    for _, o in ipairs(L.objects) do if LM.isBird(o.def) then birds[#birds + 1] = o end end
    table.sort(birds, function(a, b)
        local na, nb = a.extra and a.extra.startNumber, b.extra and b.extra.startNumber
        if na and nb then return na < nb end
        if na or nb then return na ~= nil end
        return a.x > b.x
    end)
    return birds
end

-- Where the slingshot stands (world units): its place, else at the first bird.
function LM.slingPos(L)
    if L.sling then return L.sling end
    local b = LM.birdOrder(L)[1]
    if b then return {b.x * 20, b.y * 20} end
    return LM.DEFAULT_SLING
end

-- The ground is the object 'ground' to the engine.
function LM.objectName(o, i)
    if o.def == 'Ground' then return i == 1 and 'ground' or 'ground' .. i end
    return o.def .. '_' .. i
end

function LM.loaded(L)
    local blocks = blockTable.blocks
    local world, names = {}, {}
    local counts = {}
    local birds = {}
    for i, o in ipairs(L.objects) do
        local d = blocks[o.def]
        local name = LM.objectName(o, i)
        names[i] = name
        local w = defaults(d)
        w.name, w.definition, w.baseDefinition = name, o.def, ''
        w.x, w.y, w.angle, w.z_order = o.x, o.y, o.angle, o.z
        for k, v in pairs(o.extra or {}) do
            if string.sub(k, 1, 3) == 'ov.' then
                w.overrides = w.overrides or {}
                w.overrides[string.sub(k, 4)] = v
            elseif k == 'color' then
                w.color = {R = v[1], G = v[2], B = v[3], A = v[4]}
            else
                w[k] = v
            end
        end
        world[name] = w
        counts[o.def] = (counts[o.def] or 0) + 1
        if LM.isBird(o.def) then birds[#birds + 1] = w end
    end
    -- birds without a number: from the right; the first one stands at the slingshot
    local numbered = true
    for _, w in ipairs(birds) do if not w.startNumber then numbered = false end end
    if not numbered then
        table.sort(birds, function(a, b) return a.x > b.x end)
        for i, w in ipairs(birds) do w.startNumber = i end
    end
    local sling = LM.slingPos(L)
    local joints = {}
    for i, j in ipairs(L.joints) do
        local name = 'joint_' .. i
        local w = {name = name, type = j.type, end1 = names[j.a], end2 = names[j.b], coordType = j.coords,
                   x1 = j.p[1], y1 = j.p[2], x2 = j.p[3], y2 = j.p[4], collideConnected = false}
        for k, v in pairs(j.extra or {}) do w[k] = v end
        joints[name] = w
    end
    local l, t, r, b = LM.bounds(L)
    if not l then l, t, r, b = sling[1] + 600, -400, sling[1] + 1400, 100 end
    local ground = b + 60
    local castle = camera(l - 200, t - 260, r + 200, ground)
    local bird = camera(sling[1] - 140, sling[2] - 420, sling[1] + 1470, sling[2] - 60)
    local p = L.particles
    local particles = {firstFrame = true, x = (l + r) * 0.5, y = t - 1200, width = (r - l) + 4000}
    if p then
        particles = {firstFrame = true, x = p.x, y = p.y, width = p.width}
        if p.name then
            particles.particles = p.name
            particles.settingsBegin = {amount = p.start, ignoreLimits = false}
            particles.settingsFrame = {amount = p.frame, ignoreLimits = false}
        end
    end
    local boom = L.boom or {0, 0, 0, 0}
    return {
        world = world, joints = joints, chains = {}, counts = counts, theme = L.theme, themeSprites = {},
        levelParticles = particles, castleCameraData = castle, birdCameraData = bird,
        birdZeroPosX = L.sling and L.sling[1], birdZeroPosY = L.sling and L.sling[2], physicsToWorld = 20, worldGravity = L.gravity or 20,
        BoomBoxSpawnRect = {left = boom[1], top = boom[2], right = boom[3], bottom = boom[4]},
        filename = LM.FILE,
    }
end

-- A level from the game's level data (gamelua.loadedObjects).
local OBJECT_EXTRAS = {'scaleX', 'scaleY', 'filterGroup', 'startNumber', 'destroyedScoreInc'}
local JOINT_EXTRAS = {'collideConnected', 'breakable', 'breakForce', 'upperLimit', 'lowerLimit', 'limit', 'motor', 'motorSpeed',
                      'maxTorque', 'backAndForth', 'destroyTimer', 'dampingRatio', 'frequency', 'worldAxisX', 'worldAxisY'}
function LM.fromLoaded(lo)
    local blocks = blockTable.blocks
    local L = {theme = lo.theme, gravity = lo.worldGravity, limits = {}, objects = {}, joints = {}}
    if lo.birdZeroPosX and lo.birdZeroPosY then L.sling = {lo.birdZeroPosX, lo.birdZeroPosY} end
    local p = lo.levelParticles
    if p and p.x then
        L.particles = {x = p.x, y = p.y, width = p.width}
        if p.particles then
            L.particles.name = p.particles
            L.particles.start = (p.settingsBegin or {}).amount or 0
            L.particles.frame = (p.settingsFrame or {}).amount or 0
        end
    end
    local bb = lo.BoomBoxSpawnRect
    if bb and (bb.left ~= 0 or bb.right ~= 0 or bb.top ~= 0 or bb.bottom ~= 0) then L.boom = {bb.left, bb.top, bb.right, bb.bottom} end
    local names = {}
    for name in pairs(lo.world or {}) do names[#names + 1] = name end
    table.sort(names)
    local index = {}
    for _, name in ipairs(names) do
        local w = lo.world[name]
        local d = blocks[w.definition]
        if d then
            local o = {def = w.definition, x = w.x, y = w.y, angle = w.angle or 0, z = w.z_order or 0, extra = {}}
            local def = defaults(d)
            for _, k in ipairs({'strength', 'density', 'defence'}) do
                if w[k] ~= nil and (def[k] == nil or math.abs(w[k] - def[k]) > 1e-3) then o.extra[k] = w[k] end
            end
            for _, k in ipairs(OBJECT_EXTRAS) do if w[k] ~= nil then o.extra[k] = w[k] end end
            if type(w.color) == 'table' then o.extra.color = {w.color.R or 255, w.color.G or 255, w.color.B or 255, w.color.A or 255} end
            if type(w.overrides) == 'table' then
                for k, v in pairs(w.overrides) do if type(v) == 'number' or type(v) == 'boolean' then o.extra['ov.' .. k] = v end end
            end
            L.objects[#L.objects + 1] = o
            index[name] = #L.objects
        end
    end
    local jnames = {}
    for name in pairs(lo.joints or {}) do jnames[#jnames + 1] = name end
    table.sort(jnames)
    for _, name in ipairs(jnames) do
        local w = lo.joints[name]
        local a, b = index[w.end1], index[w.end2]
        if a and b then
            local j = {type = w.type, a = a, b = b, coords = w.coordType or 0, p = {w.x1 or 0, w.y1 or 0, w.x2 or 0, w.y2 or 0}, extra = {}}
            for _, k in ipairs(JOINT_EXTRAS) do if w[k] ~= nil then j.extra[k] = w[k] end end
            if j.extra.collideConnected == false then j.extra.collideConnected = nil end
            L.joints[#L.joints + 1] = j
        end
    end
    return L
end

-- Scores for two and three stars: set when the level was beaten, else an estimate.
function LM.starLimits(L)
    if L.stars then return L.stars[1], L.stars[2] end
    local pigs, blocks, birds = 0, 0, 0
    for _, o in ipairs(L.objects) do
        local d = blockTable.blocks[o.def]
        if d.group == 'piglettes' then pigs = pigs + 1
        elseif LM.isBird(o.def) then birds = birds + 1
        elseif d.group ~= 'ground' then blocks = blocks + 1 end
    end
    local estimate = pigs * 5000 + blocks * 400 + math.max(birds - 1, 0) * 10000
    return math.floor(estimate * 0.5 / 1000) * 1000, math.floor(estimate * 0.75 / 1000) * 1000
end

-- ---- playing
function LM.active() return levelName == LM.LEVEL end

function LM.ensureEpisode()
    if g_episodes[LM.EPISODE] then return end
    local src = g_episodes.MLEXTRA
    local page = {}
    for k, v in pairs(src.pages[1]) do page[k] = v end
    page.folder_name = LM.FOLDER
    page.episode = LM.EPISODE
    page.levels = {{name = LM.LEVEL, episode = LM.EPISODE, page = 1, index = 1, index_in_episode = 1, display_number = 1}}
    local ep = {}
    for k, v in pairs(src) do ep[k] = v end
    ep.pages = {page}
    ep.level_list = {LM.LEVEL}
    ep.name = 'levelmaker'
    ep.hidden = true
    g_episodes[LM.EPISODE] = ep
end

-- Sprites a level needs beyond its theme's.
local function add_sprites(d, list, seen)
    local function add(s) if type(s) == 'string' and s ~= '' and not seen[s] then seen[s] = true list[#list + 1] = s end end
    add(d.sprite)
    for _, s in pairs(type(d.damageSprites) == 'table' and d.damageSprites or {}) do
        if type(s) == 'table' then for _, s2 in pairs(s) do add(s2) end else add(s) end
    end
end
function LM.sprites(defs)
    local list, seen = {}, {}
    for def in pairs(defs) do
        local d = blockTable.blocks[def]
        if d then add_sprites(d, list, seen) end
    end
    return list
end

-- mode: 'play' (a saved or loaded level), 'test' (from the editor), 'edit'
function LM.start(L, mode)
    LM.level, LM.mode, LM.used, LM.cheered = L, mode, {}, nil
    LM.data = LM.loaded(L)
    LM.ensureEpisode()
    local two, three = LM.starLimits(L)
    starTable[LM.LEVEL] = {silverScore = two, goldScore = three, eagleScore = math.floor(three * 1.5)}
    local slot = LM.slot and LM.store().slots[LM.slot]
    highscores[LM.LEVEL] = nil
    if mode == 'play' and slot and (slot.best or 0) > 0 then
        highscores[LM.LEVEL] = {score = slot.best, completed = false, birds = 0, lowScore = 0}
    end
    LM.result = nil
    eventManager:notify({id = events.EID_CHANGE_LEVEL, from = 'levelselection menu',
        data = {episode = LM.EPISODE, page = 1, level = 1, levelName = LM.LEVEL}})
end

-- The result of the level just played: its score when won.
function LM.takeResult()
    local h = highscores[LM.LEVEL]
    local won = LM.result
    if not won and h and h.completed and (h.score or 0) > 0 then won = h.score end
    LM.result = nil
    highscores[LM.LEVEL] = nil
    return won
end

function LM.recordWin(score)
    if LM.mode == 'test' and LM.editorWin then
        LM.editorWin(score)
    elseif LM.mode == 'play' and LM.slot then
        local slot = LM.store().slots[LM.slot]
        if slot and slot.code == LM.code then
            slot.best = math.max(slot.best or 0, score)
            local two, three = LM.starLimits(LM.level)
            local stars = score >= three and 3 or (score >= two and 2 or 1)
            slot.stars = math.max(slot.stars or 0, stars)
            slot.beaten = slot.code
            LM.saveStore()
        end
    end
end

-- Leaving the level (menu or level end): back to the editor or the level maker's menu.
function LM.leave()
    local score = LM.takeResult()
    if score then LM.recordWin(score) end
    -- as returnToLevelSelection
    if stopIngameSounds then pcall(stopIngameSounds) end
    currentMenuPage, newMenuPage = nil, nil
    if LM.mode == 'test' or LM.mode == 'edit' then
        LM.later = function() LM.start(LM.level, 'edit') end
        return
    end
    LM.mode = nil
    LM.reopen = true
    LM.later = function() eventManager:notify({id = events.EID_CHANGE_SCENE, target = 'MAIN_MENU'}) end
end

local function wrap(t, name, make)
    local f = t[name]
    if type(f) ~= 'function' then return end
    LM.wrapped = LM.wrapped or setmetatable({}, {__mode = 'k'})
    if LM.wrapped[f] then return end
    local w = make(f)
    LM.wrapped[w] = true
    t[name] = w
end

-- The level's HUD: the pause menu's Menu button leaves as the level end's does; while
-- testing or editing, no shop buttons and no currency counters.
local hidden = {}
local function show(o, key, on)
    if not o then return end
    if not on and o.visible ~= false then o:setVisible(false) hidden[key] = o
    elseif on and hidden[key] == o then o:setVisible(true) hidden[key] = nil end
end
-- For elements their parent draws itself: no drawing and no input.
local function nothing() end
local function untouchable() return false end
local function mute(o, key, on)
    if not o then return end
    if not on and hidden[key] ~= o then
        rawset(o, 'draw', nothing) rawset(o, 'hitTest', untouchable) hidden[key] = o
    elseif on and hidden[key] == o then
        rawset(o, 'draw', nil) rawset(o, 'hitTest', nil) hidden[key] = nil
    end
end
function LM.hud()
    local root = menuManager and menuManager.menus and menuManager.menus.root
    local building = LM.active() and (LM.mode == 'test' or LM.mode == 'edit')
    local igc = ingameCurrencySystem
    if igc and igc.ingameCurrencyFrame then show(igc.ingameCurrencyFrame, 'currency', not building) end
    if not root or root.name ~= 'gameHud' then return end
    local cls = getmetatable(root) and getmetatable(root).__index
    if type(cls) == 'table' then
        wrap(cls, 'backToLevelSelection', function(f) return function(self, ...)
            if LM.active() and LM.mode then return LM.leave() end
            return f(self, ...)
        end end)
    end
    local pause = root:getChild('pausePage')
    local frame = pause and pause:getChild('dummyFrame')
    local middle = frame and frame:getChild('middleButtonsFrame')
    show(middle and middle:getChild('shop_button'), 'pauseShop', not building)
    local powerups = root:getChild('powerup_button')
    local custom = LM.active() and LM.mode ~= nil
    mute(powerups and powerups:getChild('powerup_shop_button'), 'powerupShop', not custom)
    -- the "+" (buy more) on the power-ups and the TNT drop
    for i = 1, 4 do
        local p = powerups and powerups:getChild('powerup' .. i)
        mute(p and p:getChild('tag'), 'tag' .. i, not custom)
    end
    mute(root:getChild('eagleButton'), 'eagle', not custom)
    local tnt = root:getChild('blackBirdButton')
    mute(tnt and tnt:getChild('tag'), 'tagTnt', not custom)
end


-- In a custom level (test or play, not the editor).
function LM.playing() return LM.active() and LM.level ~= nil and (LM.mode == 'test' or LM.mode == 'play') end
LM.used = LM.used or {}
-- Uses left of a power-up of the level; nil for other items.
function LM.left(name)
    local n = LM.level and LM.limit(LM.level, name)
    if not n then return nil end
    return math.max(0, n - (LM.used[name] or 0))
end

-- Switches the running level's music to that of L.
function LM.playMusic(L)
    local env = LM.env
    if not env then return end
    local want = LM.musicOf(L)
    if env.currentMusic == want then return end
    if env.currentMusic then res.stopAudio(env.currentMusic) end
    env.currentMusic = want
end

function LM.install(env)
    LM.env = env
    local igcValue = env.ingameCurrencySystem or _G.ingameCurrencySystem
    local igcValueClass = igcValue and getmetatable(igcValue) and getmetatable(igcValue).__index
    LM.hud()
    -- the pause menu's level number: that of the hidden episode means nothing here
    wrap(env, 'getLevelDisplayNumber', function(f) return function(...)
        if LM.active() and LM.mode then return '' end
        return f(...)
    end end)
    wrap(env, 'loadLevel', function(f) return function(file, ...)
        if file == LM.FILE and LM.data then
            local results = {f(LM.BASE, ...)}
            env.loadedObjects = LM.copy(LM.data)
            return unpack(results)
        end
        return f(file, ...)
    end end)
    if env.dynamic then
        wrap(env.dynamic, 'enterIngame', function(f) return function(...)
            if not LM.active() or not LM.data then return f(...) end
            local defs = {}
            if LM.mode == 'edit' and LM.paletteDefs then defs = LM.paletteDefs() end
            for _, w in pairs(LM.data.world) do defs[w.definition] = true end
            local extra = LM.sprites(defs)
            -- queueInGame gets the sprites of the level's objects (and queueAssets their masks)
            local dyn = env.dynamic
            local queue, masks = dyn.queueInGame, dyn.queueAssets
            local function add(list)
                if type(list) ~= 'table' then return list end
                local c, seen = {}, {}
                for _, s in ipairs(list) do c[#c + 1] = s seen[s] = true end
                for _, s in ipairs(extra) do if not seen[s] then c[#c + 1] = s end end
                return c
            end
            dyn.queueInGame = function(list, ...) return queue(add(list), ...) end
            dyn.queueAssets = function(list, group, ...)
                if group == 'ingameMasks' then list = add(list) end
                return masks(list, group, ...)
            end
            local results = {pcall(f, ...)}
            dyn.queueInGame, dyn.queueAssets = queue, masks
            if not results[1] then error(results[2], 0) end
            return unpack(results, 2)
        end end)
    end
    wrap(env, 'startLevel', function(f) return function(...)
        local was = g_giftbox_enabled
        if LM.active() then g_giftbox_enabled = false end
        local results = {f(...)}
        if LM.active() then
            if LM.mode == 'edit' and LM.enterEditor then LM.enterEditor() end
        else
            g_giftbox_enabled = was
        end
        return unpack(results)
    end end)
    -- The game resumes its update and the physics after loading; not in the editor.
    wrap(env, 'setGameMode', function(f) return function(mode, ...)
        if LM.editing and LM.Editor and mode ~= LM.Editor.update then return end
        return f(mode, ...)
    end end)
    wrap(env, 'setPhysicsEnabled', function(f) return function(on, ...)
        if LM.editing and on then return end
        return f(on, ...)
    end end)
    wrap(env, 'returnToLevelSelection', function(f) return function(...)
        if LM.active() and LM.mode then return LM.leave() end
        return f(...)
    end end)
    -- the level end's buttons: back to the editor or the Level Maker (there is no next level)
    local ends = env.LevelEnd or _G.LevelEnd
    if ends then
        for _, name in ipairs({'returnToLevelSelection', 'returnToEpisodeSelection', 'returnToMainMenu', 'nextLevel'}) do
            wrap(ends, name, function(f) return function(self, ...)
                if LM.active() and LM.mode then
                    if res.isAudioPlaying('score_count') then res.stopAudio('score_count') end
                    return LM.leave()
                end
                return f(self, ...)
            end end)
        end
    end
    -- Power-ups in a custom level come from the level's own stock (its limits), never
    -- from the player's; the uses count again from zero when the level restarts.
    if env.PowerupManager or env.powerupManager then
        local cls = getmetatable(env.powerupManager or {})
        cls = cls and cls.__index or env.PowerupManager
        if type(cls) == 'table' then
            wrap(cls, 'canActivatePowerup', function(f) return function(self, name, ...)
                if LM.playing() and LM.left(name) and LM.left(name) <= 0 then return false end
                return f(self, name, ...)
            end end)
            wrap(cls, 'consume', function(f) return function(self, ...)
                if LM.playing() then return end
                return f(self, ...)
            end end)
            wrap(cls, 'markPowerupAsUsed', function(f) return function(self, name, ...)
                if LM.playing() then LM.used[name] = (LM.used[name] or 0) + 1 end
                return f(self, name, ...)
            end end)
            wrap(cls, 'isBoosterActive', function(f) return function(self, ...)
                if LM.playing() then return false end
                return f(self, ...)
            end end)
        end
    end
    wrap(env, 'restartLevelIngame', function(f) return function(...)
        LM.used, LM.cheered = {}, nil
        return f(...)
    end end)
    if igcValueClass then
        wrap(igcValueClass, 'getIGCValue', function(f) return function(self, name, ...)
            if LM.playing() and LM.left(name) then return LM.left(name) end
            return f(self, name, ...)
        end end)
    end
    -- No Mighty Eagle in custom levels.
    for _, name in ipairs({'isEagleEnabled', 'isEagleAvailableForShot'}) do
        wrap(env, name, function(f) return function(...)
            if LM.active() and LM.mode then return false end
            return f(...)
        end end)
    end
    -- The level end of a custom level plays game_complete instead of the star sounds.
    wrap(res, 'playAudio', function(f) return function(name, ...)
        if LM.playing() and type(name) == 'string' and string.match(name, '^star_%d$') then
            if not LM.cheered then
                LM.cheered = true
                env.playAudio('game_complete')
            end
            return
        end
        return f(name, ...)
    end end)
    -- The level's music instead of the background's.
    wrap(env, 'loadAndSetTheme', function(f) return function(...)
        local before = env.currentMusic
        local results = {f(...)}
        if LM.active() and LM.level then
            env.currentMusic = LM.musicOf(LM.level)
            if before and before ~= env.currentMusic then res.stopAudio(before) end
        end
        return unpack(results)
    end end)
    -- Rewards: custom levels give no stars or gems and a single coin a win (none in a test).
    local igc = env.ingameCurrencySystem or _G.ingameCurrencySystem
    local igcClass = igc and getmetatable(igc) and getmetatable(igc).__index
    if type(igcClass) == 'table' then
        wrap(igcClass, 'addIGCItems', function(f) return function(self, items, ...)
            if LM.active() and LM.mode and type(items) == 'table' then
                local kept = {}
                for k, v in pairs(items) do
                    if k == 'coins' then
                        local n = LM.mode == 'play' and math.min(tonumber(v) or 0, 1) or 0
                        if n > 0 then kept.coins = n end
                        LM.coinsShown = n
                    elseif k ~= 'extrastars' and k ~= 'gems' then
                        kept[k] = v
                    end
                end
                if next(kept) == nil then return end
                return f(self, kept, ...)
            end
            return f(self, items, ...)
        end end)
        -- the level end's counting up, as given
        wrap(igcClass, 'addCountToItem', function(f) return function(self, item, n, ...)
            if LM.active() and LM.mode then
                if item == 'coins' then
                    if (LM.coinsShown or 0) <= 0 then return end
                    LM.coinsShown = LM.coinsShown - 1
                elseif item == 'extrastars' or item == 'gems' then
                    return
                end
            end
            return f(self, item, n, ...)
        end end)
        wrap(igcClass, 'addCoinsAnimation', function(f) return function(self, index, ...)
            if LM.active() and LM.mode and (tonumber(index) or 1) > (LM.coinsShown or 0) then return end
            return f(self, index, ...)
        end end)
    end
    if not LM.listening and env.eventManager and env.events then
        LM.listening = true
        env.eventManager:addEventListener(env.events.EID_LEVEL_COMPLETED, {eventTriggered = function(_, event)
            if LM.active() and LM.mode then
                local s = env.score or (type(event.data) == 'table' and event.data.score)
                if type(s) == 'number' and s > 0 then LM.result = s end
            end
        end})
    end
end
)";

}  // namespace slingshot::level_maker
