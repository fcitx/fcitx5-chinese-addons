/*
 * SPDX-FileCopyrightText: 2023-2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "customphrase.h"
#include <charconv>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/stringutils.h>
#include <istream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace fcitx {

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

    std::string_view alpha = line.substr(0, i);
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
        order = result.value();
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

CustomPhraseDict::CustomPhraseDict() {}

void CustomPhraseDict::load(std::istream &in, bool loadDisabled) {
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
                stringutils::unescape(value, true);
            }
            auto index = index_.exactMatchSearch(key);
            if (index_.isNoValue(index)) {
                if (data_.size() >= std::numeric_limits<int32_t>::max()) {
                    break;
                }
                index = data_.size();
                index_.set(key, index);
                data_.push_back({});
            }

            if (!loadDisabled && order < 0) {
                if (data.empty()) {
                    multiline = &dummyPhrase;
                }
            } else {
                data_[index].push_back(CustomPhrase(order, std::move(value)));
                if (data.empty()) {
                    multiline = &data_[index].back();
                }
            }
        } else if (multiline && multiline != &dummyPhrase) {
            multiline->mutableValue().append(line);
            // Always append new line, and we will pop the last new line in
            // cleanUpMultiline.
            multiline->mutableValue().append("\n");
        }
    }
    cleanUpMultiline();
}

const std::vector<CustomPhrase> *
CustomPhraseDict::lookup(std::string_view key) const {
    auto index = index_.exactMatchSearch(key);
    if (index_.isNoValue(index)) {
        return nullptr;
    }

    return &data_[index];
}

void CustomPhraseDict::save(std::ostream &out) const {
    std::string buf;
    index_.foreach(
        [&out, &buf, this](uint32_t value, size_t _len,
                           libime::DATrie<uint32_t>::position_type pos) {
            index_.suffix(buf, _len, pos);
            for (const auto &phrase : data_[value]) {
                out << buf << "," << phrase.order() << "="
                    << fcitx::stringutils::escapeForValue(phrase.value())
                    << std::endl;
            }
            return true;
        });
}

} // namespace fcitx
