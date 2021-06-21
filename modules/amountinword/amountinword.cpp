/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "amountinword.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <stack>

namespace fcitx {

AmountInWord::AmountInWord(Instance *instance) : instance_(instance) {
    // This is ok in the test.
    if (!instance_) {
        return;
    }
    deferEvent_ = instance_->eventLoop().addDeferEvent([this](EventSource *) {
        initQuickPhrase();
        return true;
    });
}

void AmountInWord::initQuickPhrase() {
    if (!quickphrase()) {
        return;
    }
    handler_ = quickphrase()->call<IQuickPhrase::addProvider>(
        [this](InputContext *ic, const std::string &input,
               const QuickPhraseAddCandidateCallback &callback) {
            if (input == "rmbdx") {
                auto selected = ic->surroundingText().selectedText();
                if (isDigtal(selected)) {
                    std::string capital = transform(selected);
                    callback(capital, capital, QuickPhraseAction::Commit);
                }
                return false;
            } else {
                return true;
            }
        });
}

std::string AmountInWord::transform(const std::string &digtal) {
    std::string digits_[10] = {"零", "壹", "贰", "叁", "肆",
                               "伍", "陆", "柒", "捌", "玖"};
    std::string unit_1[13] = {"",   "拾", "佰", "千", "万", "拾", "佰",
                              "千", "亿", "拾", "佰", "千", "万"};
    std::string unit_2[2] = {"角", "分"};
    std::string word_;
    std::string part_int;
    std::string part_dec;
    int point_pos = int(digtal.find('.'));
    //        cout<<"point_pos = "<<point_pos<<endl;
    if (point_pos == -1) {
        part_int = digtal;
    } else {
        part_int = digtal.substr(0, point_pos);
        part_dec = digtal.substr(point_pos + 1);
    }

    int part_int_size = part_int.size();
    //        cout<<"part_int_size = "<<part_int_size<<endl;
    bool zero_flag = true;       // flase表示有0
    bool prev_zero_flag = false; // false表示0位之前有非零数
    std::stack<std::string> result;

    for (int i = 0; i < part_int_size; ++i) {
        //            cout<<"i = "<<i<<endl;
        int tmp = int(part_int[part_int_size - i - 1]) - 48;
        if (i % 4 == 0) {
            if (tmp == 0) {

                if (!zero_flag && prev_zero_flag) {
                    result.push(digits_[0]);
                }
                result.push(unit_1[i]);
                zero_flag = false;
                prev_zero_flag = false;
            } else {
                // 101
                if (!zero_flag && prev_zero_flag) {
                    result.push(digits_[0]);
                }
                result.push(unit_1[i]);
                result.push(digits_[tmp]);

                zero_flag = true;
                prev_zero_flag = true;
            }
        } else {
            if (tmp == 0) {
                zero_flag = false;
                continue;
            } else {
                if (prev_zero_flag && !zero_flag) {
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
    while (!result.empty()) {
        tmp = result.top();
        result.pop();
        if (tmp == "亿" && result.top() == "万") {
            result.pop();
        }

        word_.append(tmp);
        //            result.pop();
    }
    if (point_pos == -1) {
        word_.append("元整");
    } else {
        word_.append("元");
        for (int i = 0; i < part_dec.size(); ++i) {
            word_.append(digits_[int(part_dec[i]) - 48]);
            word_.append(unit_2[i]);
        }
    }
    return word_;
}

bool AmountInWord::isDigtal(const std::string &digtal) {
    int c = digtal.find(".");
    if (c > 0 && digtal.length() - c - 1 > 2) {
        return false;
    }

    bool isDot = false;
    for (int i = 0; i < digtal.length(); i++) {
        int tmp = (int)digtal[i];
        if ((tmp >= 48 && tmp <= 57)) {
            continue;
        } else if (!isDot == true && tmp == 46) {
            isDot = true;
            continue;
        } else {
            return false;
        }
    }
    return true;
}

class AmountInWordModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new AmountInWord(manager->instance());
    }
};
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::AmountInWordModuleFactory)
