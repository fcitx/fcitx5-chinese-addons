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
    std::vector<std::pair<std::string, std::string>>
    lookupStroke(const std::string &input, int limit);
    std::string prettyStrokeString(const std::string &input);

    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, lookup);
    FCITX_ADDON_EXPORT_FUNCTION(PinyinHelper, lookupStroke);
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
