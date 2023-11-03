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
            if (input != "duyin") {
                return true;
            }
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
        });
}

std::vector<std::string> PinyinHelper::lookup(uint32_t chr) {
    if (lookup_.load()) {
        return lookup_.lookup(chr);
    }
    return {};
}

std::vector<std::tuple<std::string, std::string, int>>
PinyinHelper::fullLookup(uint32_t chr) {
    if (lookup_.load()) {
        return lookup_.fullLookup(chr);
    }
    return {};
}

void PinyinHelper::loadStroke() { stroke_.loadAsync(); }

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

class PinyinHelperModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new PinyinHelper(manager->instance());
    }
};
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PinyinHelperModuleFactory)
