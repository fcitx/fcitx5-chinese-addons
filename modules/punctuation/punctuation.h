/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PUNCTUATION_PUNCTUATION_H_
#define _PUNCTUATION_PUNCTUATION_H_

#include "punctuation_public.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>

FCITX_CONFIGURATION(
    PunctuationConfig,
    fcitx::Option<fcitx::KeyList> hotkey{
        this, "Hotkey", _("Toggle key"), {fcitx::Key("Control+period")}};
    fcitx::HiddenOption<bool> enabled{this, "Enabled", "Enabled", true};);

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
    class ToggleAction : public fcitx::Action {
    public:
        ToggleAction(Punctuation *parent) : parent_(parent) {}

        std::string shortText(fcitx::InputContext *) const override {
            return parent_->enabled() ? _("Full width punctuation")
                                      : _("Half width punctuation");
        }
        std::string icon(fcitx::InputContext *) const override {
            return parent_->enabled() ? "fcitx-punc-active"
                                      : "fcitx-punc-inactive";
        }

        void activate(fcitx::InputContext *ic) override {
            return parent_->setEnabled(!parent_->enabled(), ic);
        }

    private:
        Punctuation *parent_;
    };

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
    void save() override {
        fcitx::safeSaveAsIni(config_, "conf/punctuation.conf");
    }
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override {
        config_.load(config, true);
        fcitx::safeSaveAsIni(config_, "conf/punctuation.conf");
        reloadConfig();
    }

    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, getPunctuation);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, pushPunctuation);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, cancelLast);

    bool enabled() const { return *config_.enabled; }
    void setEnabled(bool enabled, fcitx::InputContext *ic) {
        if (enabled != *config_.enabled) {
            config_.enabled.setValue(enabled);
            toggleAction_.update(ic);
        }
    }

    bool inWhiteList(fcitx::InputContext *inputContext) const;

private:
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());

    fcitx::Instance *instance_;
    fcitx::FactoryFor<PunctuationState> factory_;
    fcitx::ScopedConnection commitConn_, keyEventConn_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        eventWatchers_;
    std::unordered_map<std::string, PunctuationProfile> profiles_;
    PunctuationConfig config_;
    ToggleAction toggleAction_{this};
};

class PunctuationFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new Punctuation(manager->instance());
    }
};

#endif // _PUNCTUATION_PUNCTUATION_H_
