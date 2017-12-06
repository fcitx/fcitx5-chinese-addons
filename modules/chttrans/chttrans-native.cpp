/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see the file COPYING. If not,
 * see <http://www.gnu.org/licenses/>.
 */
#include "chttrans-native.h"
#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>

#define TABLE_GBKS2T "data/gbks2t.tab"

using namespace fcitx;

typedef std::unique_ptr<FILE, decltype(&std::fclose)> ScopedFILE;

bool NativeBackend::loadOnce() {
    auto file = StandardPath::global().open(StandardPath::Type::PkgData,
                                            TABLE_GBKS2T, O_RDONLY);
    if (file.fd() < 0) {
        return false;
    }

    FILE *f = fdopen(file.fd(), "rb");
    if (!f) {
        return false;
    }
    ScopedFILE fp{f, fclose};
    file.release();

    char *strBuf = nullptr;
    size_t bufLen = 0;
    while (getline(&strBuf, &bufLen, fp.get()) != -1) {
        char *simpStart = strBuf, *tradStart, *end;
        uint32_t simp, trad;

        tradStart = fcitx_utf8_get_char(strBuf, &simp);
        end = fcitx_utf8_get_char(tradStart, &trad);
        if (!s2tMap_.count(simp)) {
            s2tMap_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(simp),
                            std::forward_as_tuple(tradStart, end - tradStart));
        }
        if (!t2sMap_.count(trad)) {
            t2sMap_.emplace(
                std::piecewise_construct, std::forward_as_tuple(trad),
                std::forward_as_tuple(simpStart, tradStart - simpStart));
        }
    }
    free(strBuf);
    return true;
}

std::string convert(const std::unordered_map<uint32_t, std::string> &transMap,
                    const std::string &strHZ) {
    auto len = utf8::length(strHZ);
    std::string result;
    auto ps = strHZ.c_str();
    for (size_t i = 0; i < len; ++i) {
        uint32_t wc;
        char *nps;
        nps = fcitx_utf8_get_char(ps, &wc);
        int chr_len = nps - ps;
        auto iter = transMap.find(wc);
        if (iter != transMap.end()) {
            result.append(iter->second);
        } else {
            result.append(ps, chr_len);
        }

        ps = nps;
    }

    return result;
}

std::string NativeBackend::convertSimpToTrad(const std::string &strHZ) {
    return convert(s2tMap_, strHZ);
}

std::string NativeBackend::convertTradToSimp(const std::string &strHZ) {
    return convert(t2sMap_, strHZ);
}
