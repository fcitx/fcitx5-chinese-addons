--
-- SPDX-FileCopyrightText: 2010 Peng Wu <alexepico@gmail.com>
-- SPDX-FileCopyrightText: 2020 Weng Xuetian <wengxt@gmail.com>
--
-- SPDX-License-Identifier: GPL-2.0-or-later
--
---- encoding: UTF-8
local fcitx = require("fcitx")

local _CHINESE_DIGITS = {
  [0] = "〇",
  [1] = "一",
  [2] = "二",
  [3] = "三",
  [4] = "四",
  [5] = "五",
  [6] = "六",
  [7] = "七",
  [8] = "八",
  [9] = "九",
  [10] = "十",
}
local _DATE_PATTERN = "^(%d+)-(%d+)-(%d+)$"
local _TIME_PATTERN = "^(%d+):(%d+)$"
local _MONTH_TABLE_LEAF = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }

local function get_chinese_math_num(num)
  local ret
    if num < 10 then
        ret = _CHINESE_DIGITS[num]
    elseif num < 20 then
        ret = _CHINESE_DIGITS[10]
        if num > 10 then
            ret = ret .. _CHINESE_DIGITS[num % 10]
        end
    elseif num < 100 then
        local mod = num % 10
        ret = _CHINESE_DIGITS[(num - mod) / 10] .. _CHINESE_DIGITS[10]
        if mod > 0 then
            ret = ret .. _CHINESE_DIGITS[mod]
        end
    else
        error("Invalid number")
    end
    return ret
end

local function get_chinese_non_math_num(num)
    local ret = ""
    for ch in tostring(num):gmatch(".") do
        if ch >= "0" and ch <= "9" then
            ch = _CHINESE_DIGITS[tonumber(ch)]
        end
        ret = ret .. ch
    end
    return ret
end

local function _verify_time(hour, minute)
    if hour < 0 or hour > 23 or minute < 0 or minute > 59 then
        error("Invalid time")
    end
end

local function _verify_date(month, day)
    if month < 1 or month > 12 or day < 1 or day > _MONTH_TABLE_LEAF[month] then
        error("Invalid date")
    end
end

local function _verify_date_with_year(year, month, day)
    _verify_date(month, day)
    if year < 1 or year > 9999 then
        error("Invalid year")
    end
    if month == 2 and day == 29 then
        if year % 400 ~= 0 and year % 100 == 0 then
            error("Invalid lunar day")
        end
        if year % 4 ~= 0 then
            error("Invalid lunar day")
        end
    end
end

local function get_chinese_date(y, m, d, full)
    if full then
        return get_chinese_non_math_num(y) .. "年" ..
               get_chinese_math_num(m) .. "月" ..
               get_chinese_math_num(d) .. "日"
    else
        return y .. "年" .. m .. "月" .. d .. "日"
    end
end

local function get_chinese_time(h, m, full)
    if full then
        local ret = get_chinese_math_num(h) .. "时"
        if m > 0 then
            ret = ret .. get_chinese_math_num(m) .. "分"
        end
        return ret
    else
        return h .. "时" .. m .. "分"
    end
end

local function normalize_date(y, m, d)
    return string.format("%d-%02d-%02d", y, m, d)
end

local function normalize_time(h, m)
    return string.format("%02d:%02d", h, m)
end

function pinyin_get_time(input)
    if fcitx.currentInputMethod() ~= "pinyin" and fcitx.currentInputMethod() ~= "shuangpin"  then
        return nil
    end

    local now = input
    if #input == 0 then
        now = os.date("%H:%M")
    end
    local hour, minute
    now:gsub(_TIME_PATTERN, function(h, m)
        hour = tonumber(h)
        minute = tonumber(m)
    end)
    _verify_time(hour, minute)
    return {
        normalize_time(hour, minute),
        get_chinese_time(hour, minute, false),
        get_chinese_time(hour, minute, true),
    }
end

function pinyin_get_date(input)
    if fcitx.currentInputMethod() ~= "pinyin" and fcitx.currentInputMethod() ~= "shuangpin"  then
        return nil
    end

    local now = input
    if #input == 0 then
        now = os.date("%Y-%m-%d")
    end
    local year, month, day
    now:gsub(_DATE_PATTERN, function(y, m, d)
        year = tonumber(y)
        month = tonumber(m)
        day = tonumber(d)
    end)
    _verify_date_with_year(year, month, day)
    return {
        normalize_date(year, month, day),
        get_chinese_date(year, month, day, false),
        get_chinese_date(year, month, day, true),
    }
end

function pinyin_get_current_time()
    return pinyin_get_time("")
end

function pinyin_get_today()
    return pinyin_get_date("")
end

local _MATH_SYMBOL = {
"＋",
"－",
"＜",
"＝",
"＞",
"±",
"×",
"÷",
"∈",
"∏",
"∑",
"∕",
"√",
"∝",
"∞",
"∟",
"∠",
"∣",
"∥",
"∧",
"∨",
"∩",
"∪",
"∫",
"∮",
"∴",
"∵",
"∶",
"∷",
"∽",
"≈",
"≌",
"≒",
"≠",
"≡",
"≤",
"≥",
"≦",
"≧",
"≮",
"≯",
"⊕",
"⊙",
"⊥",
"⊿",
}

local _ROMAN_NUMBER = {
"ⅰ",
"ⅱ",
"ⅲ",
"ⅳ",
"ⅴ",
"ⅵ",
"ⅶ",
"ⅷ",
"ⅸ",
"ⅹ",
"ⅰ",
"ⅱ",
"ⅲ",
"ⅳ",
"ⅴ",
"ⅵ",
"ⅶ",
"ⅷ",
"ⅸ",
"ⅹ",
}

local _UPPER_GREEK_LETTER = {
"Α",
"Β",
"Γ",
"Δ",
"Ε",
"Ζ",
"Η",
"Θ",
"Ι",
"Κ",
"Λ",
"Μ",
"Ν",
"Ξ",
"Ο",
"Π",
"Ρ",
"Σ",
"Τ",
"Υ",
"Φ",
"Χ",
"Ψ",
"Ω",
}

local _LOWER_GREEK_LETTER = {
"α",
"β",
"γ",
"δ",
"ε",
"ζ",
"η",
"θ",
"ι",
"κ",
"λ",
"μ",
"ν",
"ξ",
"ο",
"π",
"ρ",
"σ",
"τ",
"υ",
"φ",
"χ",
"ψ",
"ω",
}

local _UPPER_RUSSIAN_LETTER = {
"А",
"Б",
"В",
"Г",
"Д",
"Е",
"Ж",
"З",
"И",
"Й",
"К",
"Л",
"М",
"Н",
"О",
"П",
"Р",
"С",
"Т",
"У",
"Ф",
"Х",
"Ц",
"Ч",
"Ш",
"Щ",
"Ъ",
"Ы",
"Ь",
"Э",
"Ю",
"Я",
"Ё",
}

local _LOWER_RUSSIAN_LETTER = {
"а",
"б",
"в",
"г",
"д",
"е",
"ж",
"з",
"и",
"й",
"к",
"л",
"м",
"н",
"о",
"п",
"р",
"с",
"т",
"у",
"ф",
"х",
"ц",
"ч",
"ш",
"щ",
"ъ",
"ы",
"ь",
"э",
"ю",
"я",
"ё",
}

local _NUMBER_SYMBOL = {
"①",
"②",
"③",
"④",
"⑤",
"⑥",
"⑦",
"⑧",
"⑨",
"⑩",
"⑴",
"⑵",
"⑶",
"⑷",
"⑸",
"⑹",
"⑺",
"⑻",
"⑼",
"⑽",
"⑾",
"⑿",
"⒀",
"⒁",
"⒂",
"⒃",
"⒄",
"⒅",
"⒆",
"⒇",
"⒈",
"⒉",
"⒊",
"⒋",
"⒌",
"⒍",
"⒎",
"⒏",
"⒐",
"⒑",
"⒒",
"⒓",
"⒔",
"⒕",
"⒖",
"⒗",
"⒘",
"⒙",
"⒚",
"⒛",
"㈠",
"㈡",
"㈢",
"㈣",
"㈤",
"㈥",
"㈦",
"㈧",
"㈨",
"㈩",
}

local _CURRENCY_SYMBOL = {
"＄",
"￠",
"￡",
"￥",
"¤",
}

local _ARROW_SYMBOL = {
"←",
"↑",
"→",
"↓",
"↖",
"↗",
"↘",
"↙",
}

function pinyin_get_symbol(input)
    if fcitx.currentInputMethod() ~= "pinyin" and fcitx.currentInputMethod() ~= "shuangpin"  then
        return nil
    end

    if input == "" then
        return {
            {suggest="sx", help="数学符号"},
            {suggest="lmsz", help="罗马数字"},
            {suggest="dxxl", help="大写希腊"},
            {suggest="xxxl", help="小写希腊"},
            {suggest="dxew", help="大写俄文"},
            {suggest="xxew", help="小写俄文"},
            {suggest="sz", help="数字符号"},
            {suggest="hb", help="货币"},
            {suggest="jt", help="箭头"},
        }
    elseif input == "sx" then
        return _MATH_SYMBOL
    elseif input == "lmsz" then
        return _ROMAN_NUMBER
    elseif input == "dxxl" then
        return _UPPER_GREEK_LETTER
    elseif input == "xxxl" then
        return _LOWER_GREEK_LETTER
    elseif input == "dxew" then
        return _UPPER_RUSSIAN_LETTER
    elseif input == "xxew" then
        return _LOWER_RUSSIAN_LETTER
    elseif input == "sz" then
        return _NUMBER_SYMBOL
    elseif input == "hb" then
        return _CURRENCY_SYMBOL
    elseif input == "jt" then
        return _ARROW_SYMBOL
    end
    return nil
end

------------
ime.register_command("fh", "pinyin_get_symbol", "输入符号", "digit", "")

ime.register_command("sj", "pinyin_get_time", "输入时间", "alpha", "输入可选时间，例如12:34")
ime.register_command("rq", "pinyin_get_date", "输入日期", "alpha", "输入可选日期，例如2013-01-01")
ime.register_trigger("pinyin_get_current_time", "显示时间", {}, {'时间'})
ime.register_trigger("pinyin_get_today", "显示日期", {}, {'日期'})
