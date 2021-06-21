/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _AMOUNTINWORD_AMOUNTINWORD_H_
#define _AMOUNTINWORD_AMOUNTINWORD_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <quickphrase_public.h>
namespace fcitx {

class AmountInWord final : public AddonInstance {
public:
    AmountInWord(Instance *instance);

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());

private:
    std::string transform(const std::string &digtal);
    bool isDigtal(const std::string &digtal);
    void initQuickPhrase();

private:
    Instance *instance_;
    std::unique_ptr<EventSource> deferEvent_;
    std::unique_ptr<HandlerTableEntry<QuickPhraseProviderCallback>> handler_;
};
} // namespace fcitx

#endif // _PINYINHELPER_PINYINHELPER_H_
