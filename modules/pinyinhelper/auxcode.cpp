/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "auxcode.h"

#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/stringutils.h>
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
    parseStream(file);
    loaded_ = true;
}

void AuxCode::loadFromFD(int fd) {
    table_.clear();
    loaded_ = false;

    IFDStreamBuf buffer(fd);
    std::istream in(&buffer);
    parseStream(in);
    loaded_ = true;
}

void AuxCode::parseStream(std::istream &in) {
    std::string buf;
    while (std::getline(in, buf)) {
        if (!utf8::validate(buf)) {
            continue;
        }
        auto line = stringutils::trimView(buf);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }
        auto character = line.substr(0, pos);
        auto code = line.substr(pos + 1);
        if (character.empty() || code.empty()) {
            continue;
        }
        if (utf8::lengthValidated(character) != 1) {
            continue;
        }
        table_[std::string(character)].push_back(std::string(code));
    }
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

    // Single character: check if any code starts with input
    if (charCount == 1) {
        auto iter = std::begin(charRange);
        std::string chr(iter.charRange().first, iter.charRange().second);
        auto it = table_.find(chr);
        if (it == table_.end()) {
            return false;
        }
        for (const auto &code : it->second) {
            if (code.starts_with(auxInput)) {
                return true;
            }
        }
        return false;
    }

    // Phrase: input length must not exceed char count
    if (inputLen > static_cast<size_t>(charCount)) {
        return false;
    }

    std::string firstCodes;
    firstCodes.reserve(static_cast<size_t>(charCount));
    for (auto iter = std::begin(charRange); iter != std::end(charRange);
         ++iter) {
        std::string chr(iter.charRange().first, iter.charRange().second);
        auto code = getFirstCode(chr);
        if (code.empty()) {
            return false;
        }
        // Aux codes are ASCII single-byte characters, take the first byte
        firstCodes += code.front();
    }

    for (size_t i = 0; i < inputLen; ++i) {
        if (firstCodes[i] != auxInput[i]) {
            return false;
        }
    }
    return true;
}

} // namespace fcitx
