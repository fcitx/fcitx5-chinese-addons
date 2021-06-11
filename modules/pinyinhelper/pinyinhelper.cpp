/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "pinyinhelper.h"
#include <algorithm>
#include <clipboard_public.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <set>
#include <stack>

namespace fcitx {

PinyinHelper::PinyinHelper(Instance *instance) : instance_(instance) {
    // This is ok in the test.
    if (!instance_) {
        return;
    }
    deferEvent_ = instance_->eventLoop().addDeferEvent([this](EventSource *) {
        initQuickPhrase();
        return true;
    });
}

void PinyinHelper::initQuickPhrase() {
    if (!quickphrase()) {
        return;
    }
    handler_ = quickphrase()->call<IQuickPhrase::addProvider>(
        [this](InputContext *ic, const std::string &input,
               const QuickPhraseAddCandidateCallback &callback) {
            if (input == "duyin") {
                std::vector<std::string> s;
                if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                    if (auto selected = ic->surroundingText().selectedText();
                        !selected.empty()) {
                        s.push_back(std::move(selected));
                    }
                }
                if (clipboard()) {
                    if (s.empty()) {
                        auto primary = clipboard()->call<IClipboard::primary>(ic);
                        if (std::find(s.begin(), s.end(), primary) == s.end()) {
                            s.push_back(std::move(primary));
                        }
                    }
                    auto clip = clipboard()->call<IClipboard::clipboard>(ic);
                    if (std::find(s.begin(), s.end(), clip) == s.end()) {
                        s.push_back(std::move(clip));
                    }
                }

                if (s.empty()) {
                    return true;
                }
                for (const auto &str : s) {
                    if (!utf8::validate(str)) {
                        continue;
                    }
                    // Hard limit to prevent do too much lookup.
                    constexpr int limit = 20;
                    int counter = 0;
                    for (auto c : utf8::MakeUTF8CharRange(str)) {
                        auto result = lookup(c);
                        if (!result.empty()) {
                            auto py = stringutils::join(result, ", ");
                            auto display = fmt::format(_("{0} ({1})"),
                                                    utf8::UCS4ToUTF8(c), py);
                            callback(display, display,
                                    QuickPhraseAction::DoNothing);
                        }
                        if (counter >= limit) {
                            break;
                        }
                        counter += 1;
                    }
                }
                return false;
            } else if (input == "rmbdx") {
                auto selected = ic->surroundingText().selectedText();
                if(isDigtal(selected)) {
                    std::string capital =  transform(selected);
                    callback(capital, capital,
                                    QuickPhraseAction::Commit);
                }
                return false;
            } else{
                return true;
            }
        });
}

std::vector<std::string> PinyinHelper::lookup(uint32_t chr) {
    if (lookup_.load()) {
        return lookup_.lookup(chr);
    }
    return {};
}

std::vector<std::pair<std::string, std::string>>
PinyinHelper::lookupStroke(const std::string &input, int limit) {
    static const std::set<char> num{'1', '2', '3', '4', '5'};
    static const std::map<char, char> py{
        {'h', '1'}, {'s', '2'}, {'p', '3'}, {'n', '4'}, {'z', '5'}};
    if (input.empty()) {
        return {};
    }
    if (!stroke_.load()) {
        return {};
    }
    if (num.count(input[0])) {
        if (!std::all_of(input.begin(), input.end(),
                         [&](char c) { return num.count(c); })) {
            return {};
        }
        return stroke_.lookup(input, limit);
    }
    if (py.count(input[0])) {
        if (!std::all_of(input.begin(), input.end(),
                         [&](char c) { return py.count(c); })) {
            return {};
        }
        std::string converted;
        std::transform(input.begin(), input.end(),
                       std::back_inserter(converted),
                       [&](char c) { return py.find(c)->second; });
        return stroke_.lookup(converted, limit);
    }
    return {};
}

std::string PinyinHelper::reverseLookupStroke(const std::string &input) {
    if (!stroke_.load()) {
        return {};
    }
    return stroke_.reverseLookup(input);
}

std::string PinyinHelper::prettyStrokeString(const std::string &input) {
    if (!stroke_.load()) {
        return {};
    }
    return stroke_.prettyString(input);
}

std::string PinyinHelper::transform(const std::string &digtal)
{
    std::string digits_[10]={"零", "壹", "贰", "叁", "肆", "伍", "陆", "柒", "捌", "玖"};
    std::string unit_1[13] = {"", "拾", "佰", "千", "万", "拾", "佰", "千", "亿", "拾", "佰", "千","万"};
    std::string unit_2[2] = {"角", "分"};
    std::string word_;
    std::string part_int;
    std::string part_dec;
    int point_pos = int(digtal.find('.'));
//        cout<<"point_pos = "<<point_pos<<endl;
    if (point_pos ==-1)
    {
        part_int = digtal;
    }
    else
    {
        part_int = digtal.substr(0,point_pos);
        part_dec = digtal.substr(point_pos+1);
    }

    int part_int_size = part_int.size();
//        cout<<"part_int_size = "<<part_int_size<<endl;
    bool zero_flag = true; // flase表示有0
    bool prev_zero_flag = false; // false表示0位之前有非零数
    std::stack<std::string> result;

    for (int i = 0; i < part_int_size; ++i)
    {
//            cout<<"i = "<<i<<endl;
        int tmp = int(part_int[part_int_size-i-1])- 48;
        if (i%4 == 0)
        {
            if (tmp == 0)
            {

                if (!zero_flag&&prev_zero_flag)
                {
                    result.push(digits_[0]);
                }
                result.push(unit_1[i]);
                zero_flag = false;
                prev_zero_flag = false;
            }
            else
            {
                //101
                if (!zero_flag && prev_zero_flag)
                {
                    result.push(digits_[0]);
                }
                result.push(unit_1[i]);
                result.push(digits_[tmp]);

                zero_flag = true;
                prev_zero_flag = true;
            }
        }
        else
        {
            if (tmp == 0)
            {
                zero_flag = false;
                continue;
            }
            else
            {
                if (prev_zero_flag&&!zero_flag)
                {
//                        result.push(digits_[0]);
                    result.push(digits_[0]);
                }
                result.push(unit_1[i]);
                result.push(digits_[tmp]);
                prev_zero_flag = true;
                zero_flag = true;
            }
        }

    }
    std::string tmp;
    while (!result.empty())
    {
        tmp = result.top();
        result.pop();
        if (tmp=="亿" &&  result.top() == "万")
        {
            result.pop();
        }

       word_.append(tmp);
//            result.pop();
    }
    if (point_pos == -1 )
    {
        word_.append("元整");
    }
    else
    {
        word_.append("元");
        for (int i = 0; i < part_dec.size(); ++i)
        {
            word_.append(digits_[int(part_dec[i])-48]);
            word_.append(unit_2[i]);
        }

    }
    return word_;
}

bool PinyinHelper::isDigtal(const std::string &digtal)
{
    int c = digtal.find(".");
    if(c > 0 && digtal.length() - c - 1 > 2) {
        return false;
    }

    bool isDot = false;
    for (int i = 0; i < digtal.length(); i++)
    {
        int tmp = (int)digtal[i];
        if ((tmp >= 48 && tmp <= 57))
        {
            continue;
        } else if(!isDot == true && tmp == 46) {
            isDot = true;
            continue;
        }
        else
        {
            return false;
        }
    }
    return true;
}

class PinyinHelperModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new PinyinHelper(manager->instance());
    }
};
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PinyinHelperModuleFactory)
