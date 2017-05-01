/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
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
#include "punctuation.h"
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx-utils/charutils.h>
#include <fcntl.h>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>

using namespace fcitx;

namespace {
    static const std::string emptyString;
}

PunctuationProfile::PunctuationProfile(std::istream &in) {
    std::string strBuf;
    while (std::getline(in, strBuf)) {
        std::string::size_type start, end;
        std::tie(start, end) = stringutils::trimInplace(strBuf);
        if (start == end) {
            continue;
        }
        std::string text(strBuf.begin() + start, strBuf.begin() + end);
        auto tokens = stringutils::split(strBuf, FCITX_WHITESPACE);
        if (tokens.size() != 2 && tokens.size() != 3) {
            continue;
        }

        if (!std::any_of(tokens.begin(), tokens.end(), [] (const std::string &s) { return utf8::validate(s); })) {
            continue;
        }
        // we don't make # as comment here, # would be consider as a valid char
        if (utf8::length(tokens[0]) != 1) {
            continue;
        }
        auto c = utf8::getCharValidated(tokens[0]);
        decltype(puncMap_)::mapped_type p;
        p.first = tokens[1];
        if (tokens.size() > 2) {
            p.second = tokens[2];
        }
        puncMap_.emplace(c, std::move(p));
    }
}


const std::string &PunctuationProfile::getPunctuation(
    uint32_t unicode, const std::string &prev) const {
  auto iter = puncMap_.find(unicode);
  if (iter == puncMap_.end()) {
    return emptyString;
  }
  if (iter->second.second.empty()) {
    return iter->second.first;
  }
  if (prev == iter->second.first) {
    return iter->second.second;
  } else {
    return iter->second.first;
  }
}

Punctuation::Punctuation() {
    reloadConfig();
}

Punctuation::~Punctuation() {}

void Punctuation::reloadConfig()
{
    const StandardPath &sp = StandardPath::global();
    auto files = sp.multiOpen(StandardPath::Type::PkgData, "punctuation", O_RDONLY, filter::Prefix("punc.mb."));
    auto iter = profiles_.begin();
    while (iter != profiles_.end()) {
        if (!files.count("punc.mb." + iter->first)) {
            iter = profiles_.erase(iter);
        } else {
            iter++;
        }
    }

    for (const auto &file : files) {
        if (file.first.size() <= 8) {
            continue;
        }
        auto lang = file.first.substr(8);

        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.second.fd(), boost::iostreams::file_descriptor_flags::
                                      never_close_handle);
            std::istream in(&buffer);
            PunctuationProfile newProfile(in);
            profiles_[lang] = std::move(newProfile);
        } catch (const std::exception &) {
        }
    }
}


const std::string &Punctuation::getPunctuation(const std::string &language, uint32_t unicode, const std::string &prev)
{
    auto iter = profiles_.find(language);
    if (iter == profiles_.end()) {
        return emptyString;
    }

    return iter->second.getPunctuation(unicode, prev);
}


FCITX_ADDON_FACTORY(PunctuationFactory);

