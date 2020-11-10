/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "chttrans-native.h"
#include "config.h"
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>

#define TABLE_GBKS2T "chttrans/gbks2t.tab"

using namespace fcitx;

bool NativeBackend::loadOnce(const ChttransConfig &) {
    auto file = StandardPath::global().open(StandardPath::Type::PkgData,
                                            TABLE_GBKS2T, O_RDONLY);
    if (file.fd() < 0) {
        return false;
    }
    boost::iostreams::stream_buffer<boost::iostreams::file_descriptor_source>
        buffer(file.fd(),
               boost::iostreams::file_descriptor_flags::never_close_handle);
    std::istream in(&buffer);

    std::string strBuf;
    while (std::getline(in, strBuf)) {
        // Get two char.
        auto simpStart = strBuf.begin();
        uint32_t simp, trad;

        auto tradStart = utf8::getNextChar(simpStart, strBuf.end(), &simp);
        auto end = utf8::getNextChar(tradStart, strBuf.end(), &trad);
        if (!utf8::isValidChar(simp) || !utf8::isValidChar(trad)) {
            continue;
        }
        if (!s2tMap_.count(simp)) {
            s2tMap_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(simp),
                            std::forward_as_tuple(tradStart, end));
        }
        if (!t2sMap_.count(trad)) {
            t2sMap_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(trad),
                            std::forward_as_tuple(simpStart, tradStart));
        }
    }
    return true;
}

std::string convert(const std::unordered_map<uint32_t, std::string> &transMap,
                    const std::string &strHZ) {
    auto len = utf8::length(strHZ);
    std::string result;
    const auto *ps = strHZ.c_str();
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
