// SPDX-License-Identifier: GPL-3.0-or-later
// Shared mod helper: the statistics mods' achievements for Recomp Achievements,
// RecompLauncher's achievements. Its channel (docs/SPEC.md in
// github.com/elripalda/RecompAchievements) is written here; the SDK's library
// is not used.
//
// A statistics mod lists its achievements in a global table, from the script
// that knows them:
//   _G.slingshotAchievements = {
//       {name = 'Herr Helmet',                      -- the game's own, stable name
//        unlocked = function() return true end,
//        describe = function() return title, description end,
//        points = 10,                               -- 5, 10, 25 or 50
//        type = 'progression',                      -- 'progression', 'win' or nil
//        bonus = true},                             -- not needed for Beaten or Mastered
//       ...}
// The event is named after `name`: 'herr_helmet' (`key` instead gives it as is).
//
// report_achievements(), from the mod's constructor: while RecompLauncher has an
// achievements session open (RL_SESSION), the table is checked about once a
// second and each achievement unlocked is sent as an event. The ones unlocked
// before the session follow a save_loaded message, so they count as Casual.
// With --mod-option game-statistics.achievement-list=<file> the table is
// written to <file> as a Recomp Achievements events list about 10 s after it is
// there (texts in the game's language: --locale en_US for English).
// A save played with a cheat mod (common/cheats.hpp) sends no events.
#pragma once

namespace slingshot::mods {

void report_achievements();

}  // namespace slingshot::mods
