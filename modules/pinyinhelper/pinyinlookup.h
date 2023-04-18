/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINHELPER_PINYINLOOKUP_H_
#define _PINYINHELPER_PINYINLOOKUP_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fcitx {

struct PinyinLookupData {
    uint8_t consonant;
    uint8_t vocal;
    uint8_t tone;
};

class PinyinLookup {
public:
    PinyinLookup();

    bool load();
    std::vector<std::string> lookup(uint32_t hz);
    std::vector<std::tuple<std::string, std::string, int>>
    fullLookup(uint32_t hz);

private:
    std::unordered_map<uint32_t, std::vector<PinyinLookupData>> data_;
    bool loaded_ = false;
    bool loadResult_ = false;
};
} // namespace fcitx

#endif // _PINYINHELPER_PINYINLOOKUP_H_
