/*
 * SPDX-FileCopyrightText: 2023-2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "customphrase.h"
#include <algorithm>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <functional>
#include <istream>
#include <iterator>
#include <libime/core/datrie.h>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fcitx {

void normalizeData(std::vector<CustomPhrase> &data) {
    std::stable_sort(data.begin(), data.end(),
                     [](const CustomPhrase &lhs, const CustomPhrase &rhs) {
                         return lhs.order() < rhs.order();
                     });

    int currentOrder = data.front().order();
    for (auto iter = std::next(data.begin()); iter != data.end(); ++iter) {
        if (currentOrder > 0) {
            if (iter->order() <= currentOrder) {
                iter->setOrder(currentOrder + 1);
            }
        }
        currentOrder = iter->order();
    }
}

std::optional<int> parseInt(std::string_view input) {
    int out;
    const std::from_chars_result result =
        std::from_chars(input.data(), input.data() + input.size(), out);
    if (result.ec == std::errc::invalid_argument ||
        result.ec == std::errc::result_out_of_range) {
        return std::nullopt;
    }
    return out;
}

using ParseResult = std::tuple<std::string_view, int, std::string_view>;

std::optional<ParseResult> parseCustomPhraseLine(std::string_view line) {
    size_t i = 0;

    // Consume all letters.
    for (; i < line.size() &&
           (charutils::islower(line[i]) || charutils::isupper(line[i]));
         i += 1) {
    }
    if (i == 0) {
        return std::nullopt;
    }

    const std::string_view alpha = line.substr(0, i);
    if (i >= line.size() || line[i] != ',') {
        return std::nullopt;
    }
    i += 1;
    int sign = 1;
    if (i < line.size() && line[i] == '-') {
        sign = -1;
        i += 1;
    }
    const size_t orderStart = i;
    for (; i < line.size() && charutils::isdigit(line[i]); i += 1) {
    }
    if (i == orderStart || i >= line.size() || line[i] != '=') {
        return std::nullopt;
    }

    int order = 0;
    if (auto result = parseInt(line.substr(orderStart, i - orderStart))) {
        order = *result;
    }
    // Zero is invalid value.
    if (order == 0) {
        return std::nullopt;
    }
    order *= sign;

    return std::make_tuple(alpha, order, line.substr(i + 1));
}

bool isComment(std::string_view line) {
    return stringutils::startsWith(line, ";") ||
           stringutils::startsWith(line, "#");
}

inline std::tm currentTm() {
#ifdef FCITX_CUSTOM_PHRASE_TEST
    std::tm timePoint;
    timePoint.tm_year = 2023 - 1900;
    timePoint.tm_mon = 6;
    timePoint.tm_mday = 11;
    timePoint.tm_wday = 2;
    timePoint.tm_hour = 23;
    timePoint.tm_min = 16;
    timePoint.tm_sec = 6;
    return timePoint;
#else
    const std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    return fmt::localtime(currentTime);
#endif
}

int currentYear() { return currentTm().tm_year + 1900; }
int currentMonth() { return currentTm().tm_mon + 1; }
int currentDay() { return currentTm().tm_mday; }
int currentWeekday() { return currentTm().tm_wday; }
int currentHour() { return currentTm().tm_hour; }
int currentMinute() { return currentTm().tm_min; }
int currentSecond() { return currentTm().tm_sec; }
int currentHalfHour() {
    const int hour = currentHour() % 12;
    return (hour == 0) ? 12 : hour;
}

std::string toChineseYear(std::string_view num) {
    constexpr std::string_view chineseDigit[] = {
        "〇", "一", "二", "三", "四", "五", "六", "七", "八", "九",
    };
    std::string result;
    result.reserve(num.size() * 3);
    for (const char c : num) {
        assert(charutils::isdigit(c));
        result += chineseDigit[c - '0'];
    }
    return result;
}

std::string toChineseWeekDay(int num) {
    assert(num >= 0 && num < 7);
    constexpr std::string_view chineseWeekday[] = {
        "日", "一", "二", "三", "四", "五", "六",
    };
    return std::string(chineseWeekday[num]);
}

std::string toChineseTwoDigitNumber(int num, bool leadingZero) {
    assert(num >= 0 && num < 100);
    constexpr std::string_view chineseDigit[] = {
        "零", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十",
    };
    if (num == 0) {
        return std::string(chineseDigit[0]);
    }
    const int tens = num / 10;
    const int ones = num % 10;
    std::string prefix;
    if (tens == 0) {
        if (leadingZero) {
            prefix = chineseDigit[0];
        }
    } else if (tens == 1) {
        prefix = chineseDigit[10];
    } else {
        prefix = stringutils::concat(chineseDigit[tens], chineseDigit[10]);
    }
    std::string suffix;
    if (ones != 0) {
        suffix = chineseDigit[ones];
    }
    return prefix + suffix;
}

bool CustomPhrase::isDynamic() const {
    return stringutils::startsWith(value(), "#");
}

std::string CustomPhrase::evaluate(
    const std::function<std::string(std::string_view key)> &evaluator) const {
    assert(evaluator);

    if (!isDynamic()) {
        return value_;
    }
    std::string_view content = value_;
    content = content.substr(1);
    std::string output;
    output.reserve(content.size());
    size_t variableNameStart = 0;
    size_t variableNameLength = 0;

    enum class State {
        Normal,
        VariableStart,
        BracedVariable,
        Variable,
    };

    auto state = State::Normal;

    for (size_t i = 0; i < content.size();) {
        const char c = content[i];

        switch (state) {
        case State::Normal:
            if (c == '$') {
                state = State::VariableStart;
            } else {
                output += c;
            }
            i += 1;
            break;

        case State::VariableStart:
            if (c == '{') {
                variableNameStart = i + 1;
                variableNameLength = 0;
                state = State::BracedVariable;
            } else if (c == '$') {
                output += '$';
                state = State::Normal;
            } else if (charutils::islower(c) || charutils::isupper(c) ||
                       c == '_') {
                variableNameStart = i;
                variableNameLength = 1;
                state = State::Variable;
            } else {
                output += '$';
                output += c;
                state = State::Normal;
            }
            i += 1;
            break;

        case State::BracedVariable:
            if (c == '}') {
                output += evaluator(
                    content.substr(variableNameStart, variableNameLength));
                state = State::Normal;
            } else {
                variableNameLength += 1;
                state = State::BracedVariable;
            }
            i += 1;
            break;

        case State::Variable:
            if (charutils::islower(c) || charutils::isupper(c) ||
                charutils::isdigit(c) || c == '_') {
                variableNameLength += 1;
                state = State::Variable;
                i += 1;
            } else {
                output += evaluator(
                    content.substr(variableNameStart, variableNameLength));
                state = State::Normal;
            }
            break;
        }
    }

    switch (state) {
    case State::Normal:
        break;
    case State::VariableStart:
        output += '$';
        break;
    case State::BracedVariable:
        output += "${";
        output += content.substr(variableNameStart, variableNameLength);
        break;
    case State::Variable:
        output +=
            evaluator(content.substr(variableNameStart, variableNameLength));
        break;
    }

    return output;
}

std::string CustomPhrase::builtinEvaluator(std::string_view key) {
    static const std::map<std::string, std::function<std::string()>,
                          std::less<>>
        table = {
            {"year", []() { return std::to_string(currentYear()); }},
            {"year_yy",
             []() { return fmt::format("{:02d}", currentYear() % 100); }},
            {"month", []() { return std::to_string(currentMonth()); }},
            {"month_mm",
             []() { return fmt::format("{:02d}", currentMonth()); }},
            {"day", []() { return std::to_string(currentDay()); }},
            {"day_dd", []() { return fmt::format("{:02d}", currentDay()); }},
            {"weekday", []() { return std::to_string(currentWeekday()); }},
            {"fullhour", []() { return fmt::format("{:02d}", currentHour()); }},
            {"halfhour",
             []() { return fmt::format("{:02d}", currentHalfHour()); }},
            {"ampm", []() { return currentHour() < 12 ? "AM" : "PM"; }},
            {"minute", []() { return fmt::format("{:02d}", currentMinute()); }},
            {"second", []() { return fmt::format("{:02d}", currentSecond()); }},
            {"year_cn",
             []() { return toChineseYear(std::to_string(currentYear())); }},
            {"year_yy_cn",
             []() {
                 return toChineseYear(
                     fmt::format("{:02d}", currentYear() % 100));
             }},
            {"month_cn",
             []() {
                 return toChineseTwoDigitNumber(currentMonth(),
                                                /*leadingZero=*/false);
             }},
            {"day_cn",
             []() {
                 return toChineseTwoDigitNumber(currentDay(),
                                                /*leadingZero=*/false);
             }},
            {"weekday_cn", []() { return toChineseWeekDay(currentWeekday()); }},
            {"fullhour_cn",
             []() {
                 return toChineseTwoDigitNumber(currentHour(),
                                                /*leadingZero=*/false);
             }},
            {"halfhour_cn",
             []() {
                 return toChineseTwoDigitNumber(currentHalfHour(),
                                                /*leadingZero=*/false);
             }},
            {"ampm_cn", []() { return currentHour() < 12 ? "上午" : "下午"; }},
            {"minute_cn",
             []() {
                 return toChineseTwoDigitNumber(currentMinute(),
                                                /*leadingZero=*/true);
             }},
            {"second_cn",
             []() {
                 return toChineseTwoDigitNumber(currentSecond(),
                                                /*leadingZero=*/true);
             }},
        };

    auto iter = table.find(key);
    if (iter != table.end()) {
        return iter->second();
    }
    return "";
}

CustomPhraseDict::CustomPhraseDict() = default;

void CustomPhraseDict::load(std::istream &in, bool loadDisabled) {
    clear();
    std::string line;
    // Line looks like
    // [a-z]+,[-][0-9]+=phrase
    CustomPhrase *multiline = nullptr;
    // If loadDisabled is true
    CustomPhrase dummyPhrase(-1, {});
    auto cleanUpMultiline = [&multiline]() {
        if (!multiline) {
            return;
        }

        if (!multiline->value().empty()) {
            multiline->mutableValue().pop_back();
        }

        multiline = nullptr;
    };

    while (std::getline(in, line)) {
        if (!multiline) {
            if (isComment(line)) {
                continue;
            }
        }

        auto parseResult = parseCustomPhraseLine(line);
        if (parseResult) {
            cleanUpMultiline();
            auto [key, order, data] = *parseResult;
            std::string value{data};
            if (value.size() >= 2 && stringutils::startsWith(value, '"') &&
                stringutils::endsWith(value, '"')) {
                if (auto unescape = stringutils::unescapeForValue(value)) {
                    value = *unescape;
                }
            }

            if (!loadDisabled && order < 0) {
                if (data.empty()) {
                    multiline = &dummyPhrase;
                }
                continue;
            }
            auto index = index_.exactMatchSearch(key);
            if (TrieType::isNoValue(index)) {
                if (data_.size() >= std::numeric_limits<int32_t>::max()) {
                    break;
                }
                index = data_.size();
                index_.set(key, index);
                data_.push_back({});
            }
            data_[index].push_back(CustomPhrase(order, std::move(value)));
            if (data.empty()) {
                multiline = &data_[index].back();
            }
        } else if (multiline && multiline != &dummyPhrase) {
            multiline->mutableValue().append(line);
            // Always append new line, and we will pop the last new line in
            // cleanUpMultiline.
            multiline->mutableValue().append("\n");
        }
    }
    cleanUpMultiline();
    for (auto &data : data_) {
        normalizeData(data);
    }
}

const std::vector<CustomPhrase> *
CustomPhraseDict::lookup(std::string_view key) const {
    auto index = index_.exactMatchSearch(key);
    if (TrieType::isNoValue(index)) {
        return nullptr;
    }

    return &data_[index];
}

std::vector<CustomPhrase> *
CustomPhraseDict::getOrCreateEntry(std::string_view key) {

    auto index = index_.exactMatchSearch(key);
    if (TrieType::isNoValue(index)) {
        if (data_.size() >= std::numeric_limits<int32_t>::max()) {
            return nullptr;
        }
        index = data_.size();
        index_.set(key, index);
        data_.push_back({});
    }
    return &data_[index];
}

void CustomPhraseDict::addPhrase(std::string_view key, std::string_view value,
                                 int order) {
    if (order == 0) {
        return;
    }
    if (auto *entry = getOrCreateEntry(key)) {
        entry->push_back(CustomPhrase(order, std::string(value)));
    }
}

void CustomPhraseDict::pinPhrase(std::string_view key, std::string_view value) {
    removePhrase(key, value);
    if (auto *entry = getOrCreateEntry(key)) {
        // enabled item is 1 based.
        entry->insert(entry->begin(), CustomPhrase(1, std::string(value)));
        normalizeData(*entry);
    }
}

void CustomPhraseDict::removePhrase(std::string_view key,
                                    std::string_view value) {

    auto index = index_.exactMatchSearch(key);
    if (TrieType::isNoValue(index)) {
        return;
    }

    data_[index].erase(std::remove_if(data_[index].begin(), data_[index].end(),
                                      [&value](const CustomPhrase &item) {
                                          return value == item.value();
                                      }),
                       data_[index].end());
}

void CustomPhraseDict::save(std::ostream &out) const {
    std::string buf;
    index_.foreach([&out, &buf,
                    this](uint32_t value, size_t _len,
                          libime::DATrie<uint32_t>::position_type pos) {
        index_.suffix(buf, _len, pos);
        for (const auto &phrase : data_[value]) {
            auto escaped = fcitx::stringutils::escapeForValue(phrase.value());
            out << buf << "," << phrase.order() << "=";
            if (escaped.size() != phrase.value().size()) {
                // Always quote escaped value.
                if (escaped.front() != '"') {
                    out << '"';
                }
                out << escaped;
                if (escaped.back() != '"') {
                    out << '"';
                }
            } else {
                out << phrase.value();
            }
            out << '\n';
        }
        return true;
    });
}

void CustomPhraseDict::clear() {
    index_.clear();
    data_.clear();
}
} // namespace fcitx
