/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINHELPER_STROKE_H_
#define _PINYINHELPER_STROKE_H_

#include <cstdint>
#include <libime/core/datrie.h>
#include <string>
#include <unordered_map>
#include <utility>

namespace fcitx {

class Stroke {
public:
    Stroke();

    bool load();
    std::vector<std::pair<std::string, std::string>>
    lookup(std::string_view input, int limit);
    std::string prettyString(const std::string &input) const;
    std::string reverseLookup(const std::string &hanzi) const;

private:
    libime::DATrie<int32_t> dict_;
    std::unordered_map<std::string, std::string> revserseDict_;
    bool loaded_ = false;
    bool loadResult_ = false;
};
} // namespace fcitx

#endif // _PINYINHELPER_STROKE_H_
