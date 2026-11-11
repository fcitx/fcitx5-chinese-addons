/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 for Android Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "../im/pinyin/shuangpinkeyhints.h"
#include <algorithm>
#include <fcitx-utils/log.h>
#include <libime/pinyin/pinyincorrectionprofile.h>
#include <sstream>
#include <string>
#include <vector>

using namespace fcitx;
using namespace libime;

namespace {

std::string hint(const std::vector<std::string> &hints, const std::string &key,
                 size_t field) {
    FCITX_ASSERT(hints.size() % 3 == 0);
    for (size_t i = 0; i < hints.size(); i += 3) {
        if (hints[i] == key) {
            return hints[i + field];
        }
    }
    return {};
}

std::vector<std::string> builtin(ShuangpinBuiltinProfile scheme) {
    const ShuangpinProfile profile(scheme);
    return buildShuangpinKeyHints(scheme, profile.validInput());
}

std::vector<std::string> custom(const std::string &contents) {
    std::istringstream profileInput(contents);
    const ShuangpinProfile profile(profileInput);
    std::istringstream hintInput(contents);
    return buildShuangpinKeyHints(hintInput, profile.validInput());
}

void testBuiltinProfiles() {
    const auto xiaohe = builtin(ShuangpinBuiltinProfile::Xiaohe);
    FCITX_ASSERT(hint(xiaohe, "v", 1) == "zh");
    FCITX_ASSERT(hint(xiaohe, "u", 1) == "sh");
    FCITX_ASSERT(hint(xiaohe, "i", 1) == "ch");
    FCITX_ASSERT(hint(xiaohe, "l", 2) == "iang/uang");
    FCITX_ASSERT(hint(xiaohe, "t", 2) == "ue/ve");
    FCITX_ASSERT(hint(builtin(ShuangpinBuiltinProfile::MS), ";", 2) == "ing");
    const auto ziguang = builtin(ShuangpinBuiltinProfile::Ziguang);
    FCITX_ASSERT(hint(ziguang, "a", 1) == "ch");
    FCITX_ASSERT(hint(ziguang, "g", 2) == "iang/uang");
    for (auto scheme :
         {ShuangpinBuiltinProfile::Ziranma, ShuangpinBuiltinProfile::MS,
          ShuangpinBuiltinProfile::Ziguang, ShuangpinBuiltinProfile::ABC,
          ShuangpinBuiltinProfile::Zhongwenzhixing,
          ShuangpinBuiltinProfile::PinyinJiajia,
          ShuangpinBuiltinProfile::Xiaohe, ShuangpinBuiltinProfile::GB}) {
        const auto labels = builtin(scheme);
        FCITX_ASSERT(!labels.empty());
        std::vector<std::string> keys;
        for (size_t i = 0; i < labels.size(); i += 3) {
            keys.push_back(labels[i]);
        }
        FCITX_ASSERT(std::is_sorted(keys.begin(), keys.end()));
        FCITX_ASSERT(std::adjacent_find(keys.begin(), keys.end()) ==
                     keys.end());
        const PinyinCorrectionProfile correction(
            BuiltinPinyinCorrectionProfile::Qwerty);
        const ShuangpinProfile corrected(scheme, &correction);
        FCITX_ASSERT(buildShuangpinKeyHints(scheme, corrected.validInput()) ==
                     labels);
    }
}

void testCustomProfiles() {
    const auto labels = custom(
        " # comment\n方案名称=Test\n=o*\n[声母]\nch=Q\nsh=q\nch=q\nzh=w\n"
        "[韵母]\niang=R\nuang=r\ning=;\nv=:\n"
        "[音节]\nju=!?\nnot_pinyin=e\nan=zz\n");
    FCITX_ASSERT(hint(labels, "q", 1) == "ch/sh");
    FCITX_ASSERT(hint(labels, "w", 1) == "zh");
    FCITX_ASSERT(hint(labels, "r", 2) == "iang/uang");
    FCITX_ASSERT(hint(labels, ";", 2) == "ing");
    FCITX_ASSERT(hint(labels, "z", 2).empty());
    FCITX_ASSERT(hint(labels, "e", 1).empty());
    for (const auto &key : {"!", "?", ":"}) {
        FCITX_ASSERT(std::find(labels.begin(), labels.end(), key) !=
                     labels.end());
        FCITX_ASSERT(hint(labels, key, 1).empty());
        FCITX_ASSERT(hint(labels, key, 2).empty());
    }
    for (const auto &name : {"自然码", "微软", "紫光", "拼音加加", "中文之星",
                             "智能ABC", "小鹤"}) {
        FCITX_ASSERT(
            custom(std::string("方案名称=") + name + "\nch=q\niang=r\n")
                .empty());
    }
    FCITX_ASSERT(custom("方案名称=Test\nnot_pinyin=q\n").empty());
    FCITX_ASSERT(hint(custom("方案名称=Test\nch=e\ning=k\n"), "e", 1) == "ch");
    FCITX_ASSERT(hint(custom("方案名称=Test\nzh=e\niang=k\n"), "e", 1) == "zh");
}

} // namespace

int main() {
    testBuiltinProfiles();
    testCustomProfiles();
}
