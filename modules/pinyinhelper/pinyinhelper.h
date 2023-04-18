/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINHELPER_PINYINHELPER_H_
#define _PINYINHELPER_PINYINHELPER_H_

#include "pinyinhelper_public.h"
#include "pinyinlookup.h"
#include "stroke.h"
#include <fcitx-config/configuration.h>
#include <fcitx-utils/event.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <libime/core/datrie.h>
#include <quickphrase_public.h>

namespace fcitx {

class PinyinHelper final : public AddonInstance {
public:
    PinyinHelper(Instance *instance);

    std::vector<std::string> lookup(uint32_t);
    std::vector<std::tuple<std::string, std::string, int>> fullLookup(uint32_t);
    std::vector<std::pair<std::string, std::string>>
    lookupStroke(const std::string &input, int limit);
    std::string reverseLookupStroke(const std::string &input);
    std::string prettyStrokeString(const std::string &input);

    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, lookup);
    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, fullLookup);
    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, lookupStroke);
    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, reverseLookupStroke);
    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, prettyStrokeString);

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(clipboard, instance_->addonManager());

private:
    void initQuickPhrase();
    Instance *instance_;
    PinyinLookup lookup_;
    Stroke stroke_;
    std::unique_ptr<EventSource> deferEvent_;
    std::unique_ptr<HandlerTableEntry<QuickPhraseProviderCallback>> handler_;
};
} // namespace fcitx

#endif // _PINYINHELPER_PINYINHELPER_H_
