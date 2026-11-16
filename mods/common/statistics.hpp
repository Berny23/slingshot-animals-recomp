// SPDX-License-Identifier: GPL-3.0-or-later
// Shared mod helper: the game-independent part of the game-statistics pages.
// kStatisticsLua defines a local table `statistics`; put it in front of a mod's
// Lua (append_lua). `language` is the game's locale ('de_DE', 'zh_TW', ...);
// tables are keyed by the locale or its first two letters ('zh_TW', 'de').
//
//   statistics.language(locale)            -> the key: 'zh_CN', 'zh_TW' or two letters
//   statistics.texts(tables, language_fn)  -> text(key): tables[language][key],
//                                             else tables.en[key]
//   statistics.number(n, language)         -> '12,345' (en), '12.345' (de), '12 345' (fr)
//   statistics.playtime(seconds, language) -> '3 h 05 min', '3 Std. 05 Min.', '3時間05分'
//   statistics.progress(parts)             -> mean of done / total over
//                                             parts = {{label, done, total}, ...};
//                                             a part with total 0 counts as done
#pragma once

namespace slingshot::mods {

constexpr const char* kStatisticsLua = R"(
local statistics = {}
function statistics.language(locale)
    locale = tostring(locale)
    if locale == 'zh_CN' or locale == 'zh_TW' then return locale end
    return locale:sub(1, 2)
end
function statistics.texts(tables, language)
    return function(key)
        local t = tables[statistics.language(language())] or tables.en
        local s = t[key]
        if s == nil then s = tables.en[key] end
        return s
    end
end
function statistics.number(n, language)
    local s = string.format('%.0f', math.floor(n or 0))
    local sign = s:sub(1, 1) == '-' and '-' or ''
    if sign ~= '' then s = s:sub(2) end
    local SEPARATORS = {de = '.', es = '.', it = '.', pt = '.', pl = ' ', fr = ' ', ru = ' '}
    local sep = SEPARATORS[statistics.language(language)] or ','
    local out = s:reverse():gsub('(%d%d%d)', '%1' .. sep):reverse()
    return sign .. out:gsub('^%' .. sep, '')
end
function statistics.playtime(seconds, language)
    local minutes = math.floor((seconds or 0) / 60)
    local FORMATS = {
        de = '%d Std. %02d Min.', ru = '%d ч %02d мин', pl = '%d godz. %02d min', ja = '%d時間%02d分',
        zh_CN = '%d小时%02d分钟', zh_TW = '%d小時%02d分鐘',
    }
    local format = FORMATS[statistics.language(language)] or '%d h %02d min'
    return format:format(math.floor(minutes / 60), minutes % 60)
end
function statistics.progress(parts)
    if #parts == 0 then return 0 end
    local sum = 0
    for _, p in ipairs(parts) do sum = sum + (p[3] > 0 and math.min(p[2] / p[3], 1) or 1) end
    return sum / #parts
end
)";

}  // namespace slingshot::mods
