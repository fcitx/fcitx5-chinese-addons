//
// Copyright (C) 2012~2012 by Yichao Yu
// yyc1992@gmail.com
// Copyright (C) 2017~2017 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//
#include "stroke.h"

#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>
#include <queue>
#include <string_view>

namespace fcitx {

Stroke::Stroke() {}

bool Stroke::load() {
    auto file = StandardPath::global().open(
        StandardPath::Type::PkgData, "pinyinhelper/py_stroke.mb", O_RDONLY);
    if (file.fd() < 0) {
        return false;
    }

    boost::iostreams::stream_buffer<boost::iostreams::file_descriptor_source>
        buffer(file.fd(),
               boost::iostreams::file_descriptor_flags::never_close_handle);
    std::istream in(&buffer);
    std::string buf;
    auto isSpaceCheck = boost::is_any_of(" \n\t\r\v\f");
    while (!in.eof()) {
        if (!std::getline(in, buf)) {
            break;
        }
        // Validate everything first, so it's easier to process.
        if (!utf8::validate(buf)) {
            continue;
        }

        boost::trim_if(buf, isSpaceCheck);
        if (buf.empty() || buf[0] == '#') {
            continue;
        }
        std::vector<std::string> tokens;
        boost::split(tokens, buf, isSpaceCheck);
        if (tokens.size() != 2 || utf8::length(tokens[1]) != 1 ||
            tokens[0].find_first_not_of("12345") != std::string::npos) {
            continue;
        }
        std::string token = tokens[0] + '|' + tokens[1];
        dict_.set(token, 1);
    }

    return true;
}

#define DELETION_WEIGHT 5
#define INSERTION_WEIGHT 5
#define SUBSTITUTION_WEIGHT 5
#define TRANSPOSITION_WEIGHT 5

std::vector<std::pair<std::string, std::string>>
Stroke::lookup(std::string_view input, int limit) {
    std::vector<std::pair<std::string, std::string>> result;
    std::unordered_set<std::string> resultSet;
    using position_type = decltype(dict_)::position_type;
    struct LookupItem {
        position_type pos;
        std::string_view remain;
        int weight;
        int length;

        bool operator>(const LookupItem &other) const {
            return weight > other.weight;
        }
    };
    std::priority_queue<LookupItem, std::vector<LookupItem>,
                        std::greater<LookupItem>>
        q;

    // First lets check if the stroke is already a prefix of single word.
    position_type onlyMatch = decltype(dict_)::NO_PATH;
    size_t onlyMatchLength = 0;
    auto addResult = [&result, &resultSet](std::string stroke, std::string hz) {
        if (resultSet.insert(hz).second) {
            result.emplace_back(std::move(stroke), std::move(hz));
        }
    };

    if (dict_.foreach(input, [this, &onlyMatch, &onlyMatchLength](
                                 int32_t, size_t len, uint64_t pos) {
            if (!dict_.isNoPath(onlyMatch)) {
                return false;
            }
            onlyMatch = pos;
            onlyMatchLength = len;
            return true;
        })) {
        if (dict_.isValid(onlyMatch)) {
            std::string buf;
            dict_.suffix(buf, input.size() + onlyMatchLength, onlyMatch);
            if (auto idx = buf.find_last_of('|'); idx != std::string::npos) {
                addResult(buf.substr(idx + 1), buf.substr(0, idx));
            }
        }
    }
    if (result.size() >= static_cast<size_t>(limit)) {
        return result;
    }

    auto pushQueue = [&q](LookupItem &&item) {
        if (item.weight >= 10) {
            return;
        }
        q.push(std::move(item));
    };

    pushQueue(LookupItem{0, input, 0, 0});

    while (!q.empty()) {
        auto current = q.top();
        q.pop();
        if (current.remain.empty()) {
            if (!dict_.foreach(
                    "|",
                    [this, &result, &current, limit,
                     &addResult](int32_t, size_t len, uint64_t pos) {
                        std::string buf;
                        dict_.suffix(buf, current.length + 1 + len, pos);
                        addResult(buf.substr(current.length + 1),
                                  buf.substr(0, current.length));
                        if (limit > 0 &&
                            result.size() >= static_cast<size_t>(limit)) {
                            return false;
                        }
                        return true;
                    },
                    current.pos)) {
                break;
            }
        }

        // Deletion
        if (current.remain.size() >= 1) {
            pushQueue(LookupItem{current.pos, current.remain.substr(1),
                                 current.weight + DELETION_WEIGHT,
                                 current.length});
        }

        for (char i = '1'; i <= '5'; i++) {
            auto pos = current.pos;
            auto v = dict_.traverse(&i, 1, pos);
            if (dict_.isNoPath(v)) {
                continue;
            }
            if (!current.remain.empty() && current.remain[0] == i) {
                pushQueue(LookupItem{pos, current.remain.substr(1),
                                     current.weight, current.length + 1});
            } else {
                pushQueue(LookupItem{pos, current.remain,
                                     current.weight + INSERTION_WEIGHT,
                                     current.length + 1});
                if (!current.remain.empty()) {
                    pushQueue(LookupItem{pos, current.remain.substr(1),
                                         current.weight + SUBSTITUTION_WEIGHT,
                                         current.length + 1});
                }
            }

            if (current.remain.size() >= 2 && current.remain[1] == i) {
                auto nextPos = pos;
                auto nextV = dict_.traverse(&current.remain[0], 1, nextPos);
                if (!dict_.isNoPath(nextV)) {
                    pushQueue(LookupItem{nextPos, current.remain.substr(2),
                                         current.weight + TRANSPOSITION_WEIGHT,
                                         current.length + 2});
                }
            }
        }
    }

    return result;
}

std::string Stroke::prettyString(const std::string &input) const {
    std::string result;
    static const std::string_view stroke_table[] = {"一", "丨", "丿",
                                                    "㇏", "𠃍", ""};
    for (auto c : input) {
        if (c < '1' || c > '5') {
            return {};
        }
        auto v = stroke_table[c - '1'];
        result.append(v.begin(), v.end());
    }
    return result;
}
} // namespace fcitx
