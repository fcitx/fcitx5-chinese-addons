/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PUNCTUATION_PUNCTUATION_PUBLIC_H_
#define _PUNCTUATION_PUNCTUATION_PUBLIC_H_

#include <fcitx/addoninstance.h>
namespace fcitx {
class InputContext;
}

FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, getPunctuation,
    const std::pair<std::string, std::string> &(const std::string &language,
                                                uint32_t unicode));
FCITX_ADDON_DECLARE_FUNCTION(Punctuation, cancelLast,
                             const std::string &(const std::string &language,
                                                 InputContext *ic));
FCITX_ADDON_DECLARE_FUNCTION(Punctuation, pushPunctuation,
                             const std::string &(const std::string &language,
                                                 InputContext *ic,
                                                 uint32_t unicode));
FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, pushPunctuationV2,
    std::pair<std::string, std::string>(const std::string &language,
                                        InputContext *ic, uint32_t unicode));

FCITX_ADDON_DECLARE_FUNCTION(
    Punctuation, getPunctuationCandidates,
    std::vector<std::string>(const std::string &language, uint32_t unicode));

#endif // _PUNCTUATION_PUNCTUATION_PUBLIC_H_
