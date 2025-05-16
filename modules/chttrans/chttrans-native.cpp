/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "chttrans-native.h"
#include "chttrans.h"
#include <cstdint>
#include <fcitx-utils/fdstreambuf.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>
#include <istream>
#include <string>
#include <string_view>

#define TABLE_GBKS2T "chttrans/gbks2t.tab"

using namespace fcitx;

bool NativeBackend::loadOnce(const ChttransConfig & /*unused*/) {
    auto file =
        StandardPaths::global().open(StandardPathsType::PkgData, TABLE_GBKS2T);
    if (!file.isValid()) {
        return false;
    }

    IFDStreamBuf buffer(file.fd());
    std::istream in(&buffer);

    std::string strBuf;
    while (std::getline(in, strBuf)) {
        // Get two char.
        auto simpStart = strBuf.begin();
        uint32_t simp;
        uint32_t trad;

        auto tradStart = utf8::getNextChar(simpStart, strBuf.end(), &simp);
        auto end = utf8::getNextChar(tradStart, strBuf.end(), &trad);
        if (!utf8::isValidChar(simp) || !utf8::isValidChar(trad)) {
            continue;
        }
        std::string_view simpView(simpStart, tradStart);
        std::string_view tradView(tradStart, end);
        s2tMap_.try_emplace(std::string(simpView), tradView);
        t2sMap_.try_emplace(std::string(tradView), simpView);
    }
    return true;
}

std::string convert(const NativeBackend::MapType &transMap,
                    const std::string &strHZ) {
    auto len = utf8::lengthValidated(strHZ);
    if (len == utf8::INVALID_LENGTH) {
        return strHZ;
    }
    auto range = fcitx::utf8::MakeUTF8StringViewRange(strHZ);
    std::string result;
    for (const auto &value : range) {
        auto iter = transMap.find(value);
        if (iter != transMap.end()) {
            result.append(iter->second);
        } else {
            result.append(value);
        }
    }

    return result;
}

std::string NativeBackend::convertSimpToTrad(const std::string &strHZ) {
    return convert(s2tMap_, strHZ);
}

std::string NativeBackend::convertTradToSimp(const std::string &strHZ) {
    return convert(t2sMap_, strHZ);
}
