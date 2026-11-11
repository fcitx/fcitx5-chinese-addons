/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 for Android Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "shuangpinkeyhints.h"
#include <algorithm>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/stringutils.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/shuangpindata.h>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fcitx {
namespace {

using HintMap =
    std::map<char, std::pair<std::set<std::string>, std::set<std::string>>>;

void addMapping(HintMap &hints, char key, const std::string &pinyin) {
    using libime::PinyinEncoder;
    if (auto final = PinyinEncoder::stringToFinal(pinyin);
        final != libime::PinyinFinal::Invalid) {
        const auto &label = PinyinEncoder::finalToString(final);
        if (label.size() > 1) {
            hints[key].second.insert(label);
        }
    } else if (auto initial = PinyinEncoder::stringToInitial(pinyin);
               initial != libime::PinyinInitial::Invalid) {
        const auto &label = PinyinEncoder::initialToString(initial);
        if (label.size() > 1) {
            hints[key].first.insert(label);
        }
    }
}

std::vector<std::string>
flatten(HintMap hints,
        const libime::ShuangpinProfile::ValidInputSetType &inputKeys) {
    for (auto key : inputKeys) {
        if (key < 'a' || key > 'z') {
            hints.try_emplace(key);
        }
    }
    std::vector<std::string> result;
    for (const auto &[key, labels] : hints) {
        result.insert(result.end(), {std::string(1, key),
                                     stringutils::join(labels.first, "/"),
                                     stringutils::join(labels.second, "/")});
    }
    return result;
}

} // namespace

std::vector<std::string> buildShuangpinKeyHints(
    libime::ShuangpinBuiltinProfile profile,
    const libime::ShuangpinProfile::ValidInputSetType &inputKeys) {
    const SP_C *finals = nullptr;
    const SP_S *initials = nullptr;
    switch (profile) {
#define PROFILE_CASE(NAME, DATA)                                               \
    case libime::ShuangpinBuiltinProfile::NAME:                                \
        finals = SPMap_C_##DATA;                                               \
        initials = SPMap_S_##DATA;                                             \
        break;
        PROFILE_CASE(Ziranma, Ziranma)
        PROFILE_CASE(MS, MS)
        PROFILE_CASE(Ziguang, Ziguang)
        PROFILE_CASE(ABC, ABC)
        PROFILE_CASE(Zhongwenzhixing, Zhongwenzhixing)
        PROFILE_CASE(PinyinJiajia, PinyinJiaJia)
        PROFILE_CASE(Xiaohe, XIAOHE)
        PROFILE_CASE(GB, GB)
#undef PROFILE_CASE
    default:
        throw std::invalid_argument("Invalid Shuangpin profile");
    }
    HintMap hints;
    for (const auto *entry = finals; entry->cJP; ++entry) {
        addMapping(hints, entry->cJP, entry->strQP);
    }
    for (const auto *entry = initials; entry->cJP; ++entry) {
        addMapping(hints, entry->cJP, entry->strQP);
    }
    return flatten(std::move(hints), inputKeys);
}

std::vector<std::string> buildShuangpinKeyHints(
    std::istream &in,
    const libime::ShuangpinProfile::ValidInputSetType &inputKeys) {
    HintMap hints;
    bool reservedName = false;
    std::string buffer;
    while (std::getline(in, buffer)) {
        auto line = stringutils::trimView(buffer);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (stringutils::consumePrefix(line, "方案名称=")) {
            reservedName = line == "自然码" || line == "微软" ||
                           line == "紫光" || line == "拼音加加" ||
                           line == "中文之星" || line == "智能ABC" ||
                           line == "小鹤";
            continue;
        }
        if (reservedName) {
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string_view::npos || equal == 0 ||
            equal + 2 != line.size()) {
            continue;
        }
        addMapping(hints, charutils::tolower(line[equal + 1]),
                   std::string(line.substr(0, equal)));
    }
    return flatten(std::move(hints), inputKeys);
}

} // namespace fcitx
