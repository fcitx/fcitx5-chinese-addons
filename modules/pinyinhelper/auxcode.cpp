/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "auxcode.h"

#include <fcitx-utils/utf8.h>
#include <fstream>

namespace fcitx {

void AuxCode::loadFromFile(const std::string &path) {
    table_.clear();
    loaded_ = false;

    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }
        std::string character = line.substr(0, pos);
        std::string code = line.substr(pos + 1);
        if (character.empty() || code.empty()) {
            continue;
        }
        if (utf8::length(character) != 1) {
            continue;
        }
        table_[character].push_back(code);
    }

    loaded_ = true;
}

bool AuxCode::anyCodeStartsWith(const std::string &character,
                                const std::string &prefix) const {
    auto it = table_.find(character);
    if (it == table_.end()) {
        return false;
    }
    for (const auto &code : it->second) {
        if (code.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

std::string AuxCode::getFirstCode(const std::string &character) const {
    auto it = table_.find(character);
    if (it == table_.end() || it->second.empty()) {
        return {};
    }
    return it->second.front();
}

bool AuxCode::matchPhrase(const std::string &phrase,
                          const std::string &auxInput) const {
    if (!loaded_ || auxInput.empty()) {
        return true;
    }

    auto charRange = utf8::MakeUTF8CharRange(phrase);
    auto charCount = utf8::lengthValidated(phrase);

    if (charCount == utf8::INVALID_LENGTH || charCount == 0) {
        return true;
    }

    size_t inputLen = auxInput.size();

    if (inputLen > static_cast<size_t>(charCount)) {
        return false;
    }

    std::string firstCodes;
    firstCodes.reserve(charCount);
    for (auto iter = std::begin(charRange); iter != std::end(charRange);
         ++iter) {
        std::string chr(iter.charRange().first, iter.charRange().second);
        auto code = getFirstCode(chr);
        if (code.empty()) {
            return false;
        }
        firstCodes += code[0];
    }

    for (size_t i = 0; i < inputLen; ++i) {
        if (firstCodes[i] != auxInput[i]) {
            return false;
        }
    }
    return true;
}

} // namespace fcitx
