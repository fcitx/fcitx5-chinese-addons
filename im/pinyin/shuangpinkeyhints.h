/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 for Android Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX5_CHINESE_ADDONS_SHUANGPINKEYHINTS_H_
#define _FCITX5_CHINESE_ADDONS_SHUANGPINKEYHINTS_H_

#include <istream>
#include <libime/pinyin/shuangpinprofile.h>
#include <string>
#include <vector>

namespace fcitx {

std::vector<std::string> buildShuangpinKeyHints(
    libime::ShuangpinBuiltinProfile profile,
    const libime::ShuangpinProfile::ValidInputSetType &inputKeys);
std::vector<std::string> buildShuangpinKeyHints(
    std::istream &in,
    const libime::ShuangpinProfile::ValidInputSetType &inputKeys);

} // namespace fcitx

#endif
