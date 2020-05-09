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

function get_time(input)
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

function get_date(input)
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

function get_current_time()
    return get_time("")
end

function get_today()
    return get_date("")
end

------------
ime.register_command("sj", "get_time", "输入时间", "alpha", "输入可选时间，例如12:34")
ime.register_command("rq", "get_date", "输入日期", "alpha", "输入可选日期，例如2013-01-01")
ime.register_trigger("get_current_time", "显示时间", {}, {'时间'})
ime.register_trigger("get_today", "显示日期", {}, {'日期'})
