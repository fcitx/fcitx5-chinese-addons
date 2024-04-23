/*
 * SPDX-FileCopyrightText: 2023-2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "symboldictionary.h"
#include <cstdint>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/stringutils.h>
#include <istream>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>

namespace fcitx {

using ParseResult = std::tuple<std::string, std::string>;

std::optional<size_t> findEnclosedQuote(std::string_view str) {
    enum class UnescapeState { NORMAL, ESCAPE };
    auto state = UnescapeState::NORMAL;
    for (size_t i = 1; i < str.size(); i++) {
        switch (state) {
        case UnescapeState::NORMAL:
            if (str[i] == '\\') {
                state = UnescapeState::ESCAPE;
            } else if (str[i] == '\"') {
                return i;
            }
            break;
        case UnescapeState::ESCAPE:
            if (str[i] == '\\' || str[i] == 'n' || str[i] == '\"') {
                continue;
            } else {
                // Invalid escape.
                return std::nullopt;
            }
            state = UnescapeState::NORMAL;
            break;
        }
    }
    return std::nullopt;
}

std::optional<ParseResult> parseSymbolLine(std::string_view line) {
    line = stringutils::trimView(line);
    if (line.empty()) {
        return std::nullopt;
        ;
    }
    std::string_view key;
    size_t valueStart = std::string_view::npos;
    if (line[0] == '\"') {
        auto quote = findEnclosedQuote(line);
        if (!quote.has_value()) {
            return std::nullopt;
        }
        auto end = *quote + 1;
        key = line.substr(0, end);
        valueStart = line.find_first_not_of(FCITX_WHITESPACE, end);
        if (valueStart == end) {
            return std::nullopt;
        }
    } else {
        auto end = line.find_first_of(FCITX_WHITESPACE);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        key = line.substr(0, end);
        valueStart = line.find_first_not_of(FCITX_WHITESPACE, end);
    }
    if (valueStart == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view value = line.substr(valueStart);
    auto parsedKey = stringutils::unescapeForValue(key);
    if (!parsedKey) {
        return std::nullopt;
    }
    auto parsedValue = stringutils::unescapeForValue(value);
    if (!parsedValue) {
        return std::nullopt;
    }
    return std::make_tuple(std::move(*parsedKey), std::move(*parsedValue));
}

SymbolDict::SymbolDict() = default;

void SymbolDict::load(std::istream &in) {
    clear();
    std::string line;

    while (std::getline(in, line)) {
        auto parseResult = parseSymbolLine(line);
        if (parseResult) {
            auto [key, value] = *parseResult;
            auto index = index_.exactMatchSearch(key);
            if (TrieType::isNoValue(index)) {
                if (data_.size() >= std::numeric_limits<int32_t>::max()) {
                    break;
                }
                index = data_.size();
                index_.set(key, index);
                data_.push_back({});
            }
            data_[index].push_back(value);
        }
    }
    index_.shrink_tail();
    data_.shrink_to_fit();
}

const std::vector<std::string> *SymbolDict::lookup(std::string_view key) const {
    auto index = index_.exactMatchSearch(key);
    if (TrieType::isNoValue(index)) {
        return nullptr;
    }

    return &data_[index];
}

void SymbolDict::clear() {
    index_.clear();
    data_.clear();
}

} // namespace fcitx
