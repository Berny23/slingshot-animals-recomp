// SPDX-License-Identifier: GPL-3.0-or-later
// Share codes of custom levels: the level as text lines (what the mod's Lua writes
// and reads) <-> compact binary, deflated, as base64url text.
//
// Text lines, tokens separated by spaces:
//   theme <name>
//   gravity <n>
//   sling <x> <y>                       slingshot, world pixels
//   stars <two> <three>                 scores for two and three stars
//   limits <n>...                       how often each power-up can be used, 0 to 10
//   music <name>                        a music clip, or off; none: the background's
//   particles <x> <y> <width> [<name> <amount at start> <amount per frame>]
//   boom <left> <top> <right> <bottom>
//   o <definition> <x> <y> <angle> <z> [<key>=<value>...]       an object, physics units, radians
//   j <type> <object> <object> <coords> <x1> <y1> <x2> <y2> [<key>=<value>...]
// Objects are numbered from 1 in their order. Names are [A-Za-z0-9_], 1 to 64
// characters. Decoding checks every value; what it returns is again such text.
#pragma once

#include <optional>
#include <string>

namespace slingshot::level_maker {

// Text lines -> share code; nullopt (and the reason in error) for text it cannot take.
std::optional<std::string> encode(const std::string& text, std::string& error);
// Share code -> text lines; nullopt for anything that is not a valid code.
std::optional<std::string> decode(const std::string& code, std::string& error);

}  // namespace slingshot::level_maker
