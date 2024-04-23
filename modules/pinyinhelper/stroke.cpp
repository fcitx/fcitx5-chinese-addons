/*
 * SPDX-FileCopyrightText: 2012-2012 Yichao Yu <yyc1992@gmail.com>
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "stroke.h"

#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>
#include <libime/core/datrie.h>
#include <queue>
#include <stdexcept>
#include <string_view>

namespace fcitx {

Stroke::Stroke() {}

void Stroke::loadAsync() {
    if (loadFuture_.valid()) {
        return;
    }

    loadFuture_ = std::async(std::launch::async, []() {
        std::tuple<libime::DATrie<int32_t>, libime::DATrie<int32_t>> result;
        auto &dict = std::get<0>(result);
        auto &reverseDict = std::get<1>(result);

        auto file = StandardPath::global().open(
            StandardPath::Type::PkgData, "pinyinhelper/py_stroke.mb", O_RDONLY);
        if (file.fd() < 0) {
            throw std::runtime_error("Failed to open file");
        }

        boost::iostreams::stream_buffer<
            boost::iostreams::file_descriptor_source>
            buffer(file.fd(),
                   boost::iostreams::file_descriptor_flags::never_close_handle);
        std::istream in(&buffer);
        std::string buf;
        while (!in.eof()) {
            if (!std::getline(in, buf)) {
                break;
            }
            // Validate everything first, so it's easier to process.
            if (!utf8::validate(buf)) {
                continue;
            }

            auto line = stringutils::trimView(buf);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            auto pos = line.find_first_of(FCITX_WHITESPACE);
            if (pos == std::string::npos) {
                continue;
            }
            std::string_view key = line.substr(0, pos);
            std::string_view value =
                stringutils::trimView(line.substr(pos + 1));
            if (utf8::length(value) != 1 ||
                key.find_first_not_of("12345") != std::string::npos) {
                continue;
            }
            std::string token = stringutils::concat(key, "|", value);
            std::string reverseToken = stringutils::concat(value, "|", key);
            dict.set(token, 1);
            reverseDict.set(reverseToken, 1);
        }

        dict.shrink_tail();
        reverseDict.shrink_tail();

        return result;
    });
}

bool Stroke::load() {
    if (loaded_) {
        return loadResult_;
    }
    if (!loadFuture_.valid()) {
        loadAsync();
    }
    try {
        std::tie(dict_, revserseDict_) = loadFuture_.get();
        loadResult_ = true;
    } catch (...) {
        loadResult_ = false;
    }

    loaded_ = true;
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
    std::optional<position_type> onlyMatch;
    size_t onlyMatchLength = 0;
    auto addResult = [&result, &resultSet](std::string stroke, std::string hz) {
        if (resultSet.insert(hz).second) {
            result.emplace_back(std::move(stroke), std::move(hz));
        }
    };

    if (dict_.foreach(input, [&onlyMatch, &onlyMatchLength](int32_t, size_t len,
                                                            uint64_t pos) {
            if (onlyMatch) {
                return false;
            }
            onlyMatch = pos;
            onlyMatchLength = len;
            return true;
        })) {
        if (onlyMatch) {
            std::string buf;
            dict_.suffix(buf, input.size() + onlyMatchLength, *onlyMatch);
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
        q.push(item);
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
                        return limit <= 0 ||
                               result.size() < static_cast<size_t>(limit);
                    },
                    current.pos)) {
                break;
            }
        }

        // Deletion
        if (!current.remain.empty()) {
            pushQueue(LookupItem{current.pos, current.remain.substr(1),
                                 current.weight + DELETION_WEIGHT,
                                 current.length});
        }

        for (char i = '1'; i <= '5'; i++) {
            auto pos = current.pos;
            auto v = dict_.traverse(&i, 1, pos);
            if (libime::DATrie<int32_t>::isNoPath(v)) {
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
                auto nextV = dict_.traverse(current.remain.data(), 1, nextPos);
                if (!libime::DATrie<int32_t>::isNoPath(nextV)) {
                    pushQueue(LookupItem{nextPos, current.remain.substr(2),
                                         current.weight + TRANSPOSITION_WEIGHT,
                                         current.length + 2});
                }
            }
        }
    }

    return result;
}

std::string Stroke::reverseLookup(const std::string &hanzi) const {
    using position_type = decltype(dict_)::position_type;
    libime::DATrie<int32_t>::position_type pos = 0;
    auto result = revserseDict_.traverse(hanzi, pos);
    if (libime::DATrie<int32_t>::isNoPath(result)) {
        return {};
    }
    result = revserseDict_.traverse("|", pos);
    if (libime::DATrie<int32_t>::isNoPath(result)) {
        return {};
    }
    std::optional<position_type> onlyMatch;
    size_t onlyMatchLength = 0;
    if (revserseDict_.foreach(
            [&onlyMatch, &onlyMatchLength](int32_t, size_t len, uint64_t pos) {
                if (onlyMatch) {
                    return false;
                }
                onlyMatch = pos;
                onlyMatchLength = len;
                return true;
            },
            pos)) {
        if (onlyMatch) {
            std::string buf;
            revserseDict_.suffix(buf, onlyMatchLength, *onlyMatch);
            return buf;
        }
    }
    return {};
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
