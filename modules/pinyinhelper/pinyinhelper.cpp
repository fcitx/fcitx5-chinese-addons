//
// Copyright (C) 2017~2017 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//

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

namespace fcitx {

PinyinHelper::PinyinHelper(Instance *instance) : instance_(instance) {
    lookup_.load();
    stroke_.load();

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
               QuickPhraseAddCandidateCallback callback) {
            if (input != "duyin") {
                return true;
            }
            std::unordered_set<std::string> s;
            if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                if (auto selected = ic->surroundingText().selectedText();
                    !selected.empty()) {
                    s.insert(std::move(selected));
                }
            }
            if (clipboard()) {
                if (s.empty()) {
                    s.insert(clipboard()->call<IClipboard::primary>(ic));
                }
                s.insert(clipboard()->call<IClipboard::clipboard>(ic));
            } else {
                return false;
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
        });
}

std::vector<std::string> PinyinHelper::lookup(uint32_t chr) {
    return lookup_.lookup(chr);
}

std::vector<std::pair<std::string, std::string>>
PinyinHelper::lookupStroke(const std::string &input, int limit) {
    static const std::set<char> num{'1', '2', '3', '4', '5'};
    static const std::map<char, char> py{
        {'h', '1'}, {'s', '2'}, {'p', '3'}, {'n', '4'}, {'z', '5'}};
    if (input.empty()) {
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

std::string PinyinHelper::prettyStrokeString(const std::string &input) {
    return stroke_.prettyString(input);
}

class PinyinHelperModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new PinyinHelper(manager->instance());
    }
};
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PinyinHelperModuleFactory)
