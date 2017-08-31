/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see the file COPYING. If not,
 * see <http://www.gnu.org/licenses/>.
 */
#ifndef _PUNCTUATION_PUNCTUATION_H_
#define _PUNCTUATION_PUNCTUATION_H_

#include "punctuation_public.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>

class PunctuationProfile {
public:
    PunctuationProfile() {}
    PunctuationProfile(std::istream &in);

    PunctuationProfile(PunctuationProfile &&) = default;
    PunctuationProfile(const PunctuationProfile &) = default;

    PunctuationProfile &operator=(PunctuationProfile &&) = default;
    PunctuationProfile &operator=(const PunctuationProfile &) = default;

    const std::pair<std::string, std::string> &
    getPunctuation(uint32_t unicode) const;

private:
    std::unordered_map<uint32_t, std::pair<std::string, std::string>> puncMap_;
};

class PunctuationState;

class Punctuation final : public fcitx::AddonInstance {
public:
    Punctuation(fcitx::Instance *instance);
    ~Punctuation();

    const std::pair<std::string, std::string> &
    getPunctuation(const std::string &language, uint32_t unicode);
    const std::string &pushPunctuation(const std::string &language,
                                       fcitx::InputContext *ic,
                                       uint32_t unicode);
    const std::string &cancelLast(const std::string &language,
                                  fcitx::InputContext *ic);

    void reloadConfig() override;

    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, getPunctuation);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, pushPunctuation);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, cancelLast);

private:
    fcitx::Instance *instance_;
    fcitx::FactoryFor<PunctuationState> factory_;
    fcitx::ScopedConnection commitConn_, keyEventConn_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        eventWatchers_;
    std::unordered_map<std::string, PunctuationProfile> profiles_;
};

class PunctuationFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new Punctuation(manager->instance());
    }
};

#endif // _PUNCTUATION_PUNCTUATION_H_
