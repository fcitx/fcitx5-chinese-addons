/*
 * SPDX-FileCopyrightText: 2012-2012 Yichao Yu <yyc1992@gmail.com>
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "pinyinlookup.h"

#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>

namespace fcitx {

namespace {

std::string_view py_enhance_get_vokal(int index, int tone) {
    static const std::string_view vokals_table[][5] = {
        {"", "", "", "", ""},
        {"a", "ā", "á", "ǎ", "à"},
        {"ai", "āi", "ái", "ǎi", "ài"},
        {"an", "ān", "án", "ǎn", "àn"},
        {"ang", "āng", "áng", "ǎng", "àng"},
        {"ao", "āo", "áo", "ǎo", "ào"},
        {"e", "ē", "é", "ě", "è"},
        {"ei", "ēi", "éi", "ěi", "èi"},
        {"en", "ēn", "én", "ěn", "èn"},
        {"eng", "ēng", "éng", "ěng", "èng"},
        {"er", "ēr", "ér", "ěr", "èr"},
        {"i", "ī", "í", "ǐ", "ì"},
        {"ia", "iā", "iá", "iǎ", "ià"},
        {"ian", "iān", "ián", "iǎn", "iàn"},
        {"iang", "iāng", "iáng", "iǎng", "iàng"},
        {"iao", "iāo", "iáo", "iǎo", "iào"},
        {"ie", "iē", "ié", "iě", "iè"},
        {"in", "īn", "ín", "ǐn", "ìn"},
        {"ing", "īng", "íng", "ǐng", "ìng"},
        {"iong", "iōng", "ióng", "iǒng", "iòng"},
        {"iu", "iū", "iú", "iǔ", "iù"},
        {"m", "m", "m", "m", "m"},
        {"n", "n", "ń", "ň", "ǹ"},
        {"ng", "ng", "ńg", "ňg", "ǹg"},
        {"o", "ō", "ó", "ǒ", "ò"},
        {"ong", "ōng", "óng", "ǒng", "òng"},
        {"ou", "ōu", "óu", "ǒu", "òu"},
        {"u", "ū", "ú", "ǔ", "ù"},
        {"ua", "uā", "uá", "uǎ", "uà"},
        {"uai", "uāi", "uái", "uǎi", "uài"},
        {"uan", "uān", "uán", "uǎn", "uàn"},
        {"uang", "uāng", "uáng", "uǎng", "uàng"},
        {"ue", "uē", "ué", "uě", "uè"},
        {"ueng", "uēng", "uéng", "uěng", "uèng"},
        {"ui", "uī", "uí", "uǐ", "uì"},
        {"un", "ūn", "ún", "ǔn", "ùn"},
        {"uo", "uō", "uó", "uǒ", "uò"},
        {"ü", "ǖ", "ǘ", "ǚ", "ǜ"},
        {"üan", "üān", "üán", "üǎn", "üàn"},
        {"üe", "üē", "üé", "üě", "üè"},
        {"ün", "ǖn", "ǘn", "ǚn", "ǜn"}};
    static const int8_t vokals_count = FCITX_ARRAY_SIZE(vokals_table);
    if (index < 0 || index >= vokals_count) {
        return "";
    }
    if (tone < 0 || tone > 4) {
        tone = 0;
    }
    return vokals_table[index][tone];
}

std::string_view py_enhance_get_konsonant(int index) {
    static const std::string_view konsonants_table[] = {
        "",   "b", "c", "ch", "d", "f",  "g", "h", "j", "k", "l", "m", "n",
        "ng", "p", "q", "r",  "s", "sh", "t", "w", "x", "y", "z", "zh"};
    static const int8_t konsonants_count = FCITX_ARRAY_SIZE(konsonants_table);
    if (index < 0 || index >= konsonants_count) {
        return "";
    }
    return konsonants_table[index];
}
} // namespace

PinyinLookup::PinyinLookup() {}

std::vector<std::string> PinyinLookup::lookup(uint32_t hz) {
    auto iter = data_.find(hz);
    if (iter == data_.end()) {
        return {};
    }
    std::vector<std::string> result;
    for (const auto &data : iter->second) {
        auto c = py_enhance_get_konsonant(data.consonant);
        auto v = py_enhance_get_vokal(data.vocal, data.tone);
        if (c.empty() && v.empty()) {
            continue;
        }
        result.emplace_back(stringutils::concat(c, v));
    }
    return result;
}

std::vector<std::tuple<std::string, std::string, int>>
PinyinLookup::fullLookup(uint32_t hz) {
    auto iter = data_.find(hz);
    if (iter == data_.end()) {
        return {};
    }
    std::vector<std::tuple<std::string, std::string, int>> result;
    for (const auto &data : iter->second) {
        auto c = py_enhance_get_konsonant(data.consonant);
        auto v = py_enhance_get_vokal(data.vocal, data.tone);
        if (c.empty() && v.empty()) {
            continue;
        }
        auto noToneV = py_enhance_get_vokal(data.vocal, 0);
        result.emplace_back(stringutils::concat(c, v),
                            stringutils::concat(c, noToneV), data.tone);
    }
    return result;
}

bool PinyinLookup::load() {
    if (loaded_) {
        return loadResult_;
    }
    loaded_ = true;

    auto file = StandardPath::global().open(
        StandardPath::Type::PkgData, "pinyinhelper/py_table.mb", O_RDONLY);
    if (file.fd() < 0) {
        return false;
    }
    /**
     * Format:
     * uint8_t word_l;
     * char word[word_l];
     * uint8_t count;
     * int8_t py[count][3];
     **/
    while (true) {
        char word[FCITX_UTF8_MAX_LENGTH + 1];
        uint8_t wordLen;
        auto res = read(file.fd(), &wordLen, 1);
        if (res == 0) {
            break;
        }
        if (res < 0 || wordLen > FCITX_UTF8_MAX_LENGTH) {
            return false;
        }
        if (read(file.fd(), word, wordLen) != wordLen) {
            return false;
        }
        word[wordLen] = '\0';
        std::string_view view(word);
        if (utf8::lengthValidated(view) != 1) {
            return false;
        }
        uint32_t chr = utf8::getChar(view);
        uint8_t count;
        if (read(file.fd(), &count, 1) != 1) {
            return false;
        }
        if (count == 0) {
            continue;
        }
        auto &data = data_[chr];
        while (count--) {
            uint8_t buf[3];
            if (read(file.fd(), buf, 3) != 3) {
                return false;
            }
            data.push_back({buf[0], buf[1], buf[2]});
        }
    }
    loadResult_ = true;
    return true;
}
} // namespace fcitx
