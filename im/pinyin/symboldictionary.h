/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef _PINYIN_SYMBOLDICTIONARY_H_
#define _PINYIN_SYMBOLDICTIONARY_H_

#include <fcitx-utils/macros.h>
#include <istream>
#include <libime/core/datrie.h>
#include <string>
#include <string_view>
#include <vector>

namespace fcitx {

class SymbolDict {
public:
    using TrieType = libime::DATrie<uint32_t>;
    SymbolDict();

    void load(std::istream &in);
    void clear();

    const std::vector<std::string> *lookup(std::string_view key) const;

private:
    TrieType index_;
    std::vector<std::vector<std::string>> data_;
};

} // namespace fcitx

#endif // _PINYIN_SYMBOLDICTIONARY_H_