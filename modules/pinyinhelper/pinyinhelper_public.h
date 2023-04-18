/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINHELPER_PINYINHELPER_PUBLIC_H_
#define _PINYINHELPER_PINYINHELPER_PUBLIC_H_

#include <fcitx/addoninstance.h>
#include <string>
#include <vector>

FCITX_ADDON_DECLARE_FUNCTION(PinyinHelper, lookup,
                             std::vector<std::string>(uint32_t));
/* return with fullpinyin (in form of ü), pinyin with tone, and tone */
FCITX_ADDON_DECLARE_FUNCTION(
    PinyinHelper, fullLookup,
    std::vector<std::tuple<std::string, std::string, int>>(uint32_t));
FCITX_ADDON_DECLARE_FUNCTION(PinyinHelper, lookupStroke,
                             std::vector<std::pair<std::string, std::string>>(
                                 const std::string &, int limit));
FCITX_ADDON_DECLARE_FUNCTION(PinyinHelper, reverseLookupStroke,
                             std::string(const std::string &));
FCITX_ADDON_DECLARE_FUNCTION(PinyinHelper, prettyStrokeString,
                             std::string(const std::string &));

#endif // _PINYINHELPER_PINYINHELPER_PUBLIC_H_
