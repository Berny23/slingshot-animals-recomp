// SPDX-License-Identifier: GPL-3.0-or-later
// level-weather: the level particles (rain in Surf and Turf 31-45) show.
//
// updateGame spawns a level's particles (objects.levelParticles) with addLevelParticles,
// but the native code that creates them leaves their draw layer unset, so
// drawLevelParticlesNative never draws them. The mod spawns the weather as ordinary
// particles over the level instead (newParticles, with particle definitions of its own,
// flattened as ParticleUtils does), unless the graphics are set to low quality, as for
// the game's own. While it falls the particle limits are raised by what falls at a time,
// so the other effects keep theirs. The level maker's weather (lmSnow, lmConfetti) falls
// the same way.
#include "common/game_scripts.hpp"
#include "mod.hpp"

namespace {

constexpr const char* kLua = R"(
local W = _G.levelWeather or {wrapped = setmetatable({}, {__mode = 'k'})}
_G.levelWeather = W

-- rate: particles a second per 1000 pixels of width; life: seconds.
local function falling(sprites, scale, spin, vx, vy, life)
    return {
        sheet = 'INGAME_PARTICLES_1', sprites = sprites, lifeTime = life, gravityX = 0, gravityY = 0,
        minScaleBegin = scale[1], maxScaleBegin = scale[2], minScaleEnd = scale[1], maxScaleEnd = scale[2],
        minAngle = spin and -180 or 0, maxAngle = spin and 180 or 0, minAngleVel = -(spin or 0), maxAngleVel = spin or 0,
        emitter_box = {minVelX = vx[1], maxVelX = vx[2], minVelY = vy[1], maxVelY = vy[2]},
        minVelX = vx[1], maxVelX = vx[2], minVelY = vy[1], maxVelY = vy[2],
    }
end
local function confetti(colors)
    local sprites = {}
    for _, c in ipairs(colors) do
        for i = 1, 3 do sprites[#sprites + 1] = 'P_CONFETTI_' .. c .. '_' .. i end
    end
    return {particle = 'lw_' .. table.concat(colors), rate = 2, life = 34,
            def = falling(sprites, {1.4, 1.9}, 8, {-40, 40}, {70, 120}, 34)}
end
-- the game's rain (theme15rain): its sprite, size and speed (1800-2400 at 120 degrees),
-- turned along its way (newParticles' angle; the particle's own angles are not used).
-- Its sheet is loaded only with the storm backgrounds; elsewhere streaks of the bubble
-- particles stand in.
local RAIN = {particle = 'lw_rain', rate = 60, life = 2.5, sprite = 'THEME_15_WATERPARTICLE', angle = math.rad(120),
              def = falling({'THEME_15_WATERPARTICLE'}, {0.3, 0.5}, nil, {-1200, -900}, {1560, 2080}, 2.5),
              instead = {particle = 'lw_rain2', def = falling({'BP_BUBBLE_PARTICLE_1'}, {0.5, 0.7}, nil, {-60, -40}, {1500, 1900}, 2.5)}}
RAIN.def.sheet = ''
W.KINDS = {
    theme15rain = RAIN,
    confettisBlue = confetti({'BLUE'}), confettisGreen = confetti({'GREEN'}), confettisSilver = confetti({'SILVER'}),
    confettisBlueGreen = confetti({'BLUE', 'GREEN'}), confettisBlueSilver = confetti({'BLUE', 'SILVER'}),
    confettisGreenSilver = confetti({'GREEN', 'SILVER'}), confettisBlueGreenSilver = confetti({'BLUE', 'GREEN', 'SILVER'}),
    lmSnow = {particle = 'lw_snow', rate = 3, life = 34, def = falling({'P_SMOKE_1'}, {1, 2}, 2, {-25, 25}, {55, 95}, 34)},
    lmConfetti = confetti({'BLUE', 'GREEN', 'SILVER', 'RED'}),
}
local SOFT_LIMIT, SOFT_FACTOR, HARD_LIMIT = 40, 0.5, 125  -- the game's

local function limits(extra)
    _G.particles.setSoftLimit(SOFT_LIMIT + extra, SOFT_FACTOR)
    _G.particles.setHardLimit(HARD_LIMIT + extra)
end

-- The weather of objects.levelParticles, set up when that table changes.
local function start(lp)
    W.source = lp
    local kind = type(lp) == 'table' and lp.particles and W.KINDS[lp.particles]
    if W.current and not kind then limits(0) end
    W.current = nil
    if not kind then return end
    local particle, angle = kind.particle, kind.angle or 0
    if kind.instead and not (kind.sprite and (res.getSpriteBounds('', kind.sprite) or 0) > 0) then
        particle, angle = kind.instead.particle, 0
        particleTable.particles[particle] = kind.instead.def
    else
        particleTable.particles[particle] = kind.def
    end
    -- over the level's objects (in world pixels, 20 a physics unit) and well beyond
    local l, t, r = math.huge, -400, -math.huge
    for _, o in pairs(objects.world or {}) do
        if type(o) == 'table' and o.x and o.y then
            l, r, t = math.min(l, o.x * 20), math.max(r, o.x * 20), math.min(t, o.y * 20)
        end
    end
    if l > r then l, r = (lp.x or 0) - 2000, (lp.x or 0) + 2000 end
    l, r, t = l - 2000, r + 2000, t - 1000
    local rate = kind.rate * (r - l) / 1000
    limits(math.floor(rate * kind.life))
    W.current = {particle = particle, angle = angle, rate = rate, x = (l + r) * 0.5, width = r - l, top = t, due = 0,
                 -- slow weather starts already spread over the sky
                 warm = kind.life > 10 and math.floor(rate * kind.life * 0.6)}
end

-- One at a time: newParticles' angle turns its whole spawn area too.
local function drop(w, x, y)
    newParticles(w.particle, 1, x, y, 0, 0, w.angle, true, false)
end

-- (the engine may run this more than once: updateGame is wrapped once)
local update = updateGame
local function weatherUpdate(dt, ...)
    -- before the game's update, which hands the same name to addLevelParticles
    local lp = objects and objects.levelParticles
    if lp ~= W.source then start(lp) end
    local results = {update(dt, ...)}
    local w = W.current
    if w and not settingsWrapper:isGfxLowQuality() then
        local left = w.x - w.width * 0.5
        if w.warm then
            for _ = 1, w.warm do
                drop(w, left + math.random() * w.width, w.top + math.random() * (100 - w.top))
            end
            w.warm = nil
        end
        w.due = w.due + w.rate * (dt or 0)
        while w.due >= 1 do
            w.due = w.due - 1
            drop(w, left + math.random() * w.width, w.top)
        end
    end
    return unpack(results)
end
if update and not W.wrapped[update] then
    W.wrapped[weatherUpdate] = true
    updateGame = weatherUpdate
end
)";

class LevelWeather final : public slingshot::Mod {
public:
    LevelWeather() { slingshot::mods::append_lua("data/scripts/gamelogic.lua", kLua); }
};

}  // namespace

SLINGSHOT_MOD(LevelWeather, "level-weather", "Show the levels' weather (the rain of Surf and Turf's stormy levels)");
