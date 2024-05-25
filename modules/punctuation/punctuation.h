/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PUNCTUATION_PUNCTUATION_H_
#define _PUNCTUATION_PUNCTUATION_H_

#include "punctuation_public.h"
#include <cstdint>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/signals.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

FCITX_CONFIGURATION(
    PunctuationConfig,
    fcitx::Option<fcitx::KeyList> hotkey{
        this, "Hotkey", _("Toggle key"), {fcitx::Key("Control+period")}};
    fcitx::Option<bool> halfWidthPuncAfterLatinOrNumber{
        this, "HalfWidthPuncAfterLetterOrNumber",
        _("Half width punctuation after latin letter or number"), true};
    fcitx::Option<bool> typePairedPunctuationTogether{
        this, "TypePairedPunctuationsTogether",
        _("Type paired punctuations together (e.g. Quote)"), false};
    fcitx::HiddenOption<bool> enabled{this, "Enabled", "Enabled", true};);

FCITX_CONFIGURATION(
    PunctuationMapEntryConfig,
    fcitx::Option<std::string> key{
        this, "Key", C_("Key of the punctuation, e.g. comma", "Key")};
    fcitx::Option<std::string> mapResult1{this, "Mapping", _("Mapping")};
    fcitx::Option<std::string> mapResult2{this, "AltMapping",
                                          _("Alternative Mapping")};)

FCITX_CONFIGURATION(
    PunctuationMapConfig,
    fcitx::OptionWithAnnotation<std::vector<PunctuationMapEntryConfig>,
                                fcitx::ListDisplayOptionAnnotation>
        entries{this,
                "Entries",
                _("Entries"),
                {},
                {},
                {},
                fcitx::ListDisplayOptionAnnotation("Key")};);

class PunctuationProfile {
public:
    PunctuationProfile() = default;
    PunctuationProfile(const PunctuationProfile &) = delete;

    void loadSystem(std::istream &in);
    void load(std::istream &in);
    void set(const fcitx::RawConfig &config);
    void save(std::string_view name) const;
    void resetDefaultValue();

    const std::pair<std::string, std::string> &
    getPunctuation(uint32_t unicode) const;
    std::vector<std::string> getPunctuations(uint32_t unicode) const;
    PunctuationMapConfig &config() { return punctuationMapConfig_; }
    const PunctuationMapConfig &config() const { return punctuationMapConfig_; }

    static constexpr std::string_view profilePrefix = "punc.mb.";

private:
    void addEntry(uint32_t key, const std::string &value,
                  const std::string &value2);
    std::unordered_map<uint32_t,
                       std::vector<std::pair<std::string, std::string>>>
        puncMap_;
    PunctuationMapConfig punctuationMapConfig_;
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
    std::vector<std::string> getPunctuations(const std::string &language,
                                             uint32_t unicode);
    const std::string &pushPunctuation(const std::string &language,
                                       fcitx::InputContext *ic,
                                       uint32_t unicode);
    std::pair<std::string, std::string>
    pushPunctuationV2(const std::string &language, fcitx::InputContext *ic,
                      uint32_t unicode);
    const std::string &cancelLast(const std::string &language,
                                  fcitx::InputContext *ic);
    std::vector<std::string>
    getPunctuationCandidates(const std::string &language, uint32_t unicode);

    void reloadConfig() override;
    void save() override {
        fcitx::safeSaveAsIni(config_, "conf/punctuation.conf");
    }
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override {
        config_.load(config, true);
        fcitx::safeSaveAsIni(config_, "conf/punctuation.conf");
    }
    const fcitx::Configuration *
    getSubConfig(const std::string &path) const override;
    void setSubConfig(const std::string &path,
                      const fcitx::RawConfig &config) override;

    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, getPunctuation);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, pushPunctuation);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, pushPunctuationV2);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, cancelLast);
    FCITX_ADDON_EXPORT_FUNCTION(Punctuation, getPunctuationCandidates)

    bool enabled() const { return *config_.enabled; }
    void setEnabled(bool enabled, fcitx::InputContext *ic) {
        if (enabled != *config_.enabled) {
            config_.enabled.setValue(enabled);
            toggleAction_.update(ic);
        }
    }

    bool inWhiteList(fcitx::InputContext *inputContext) const;

private:
    void loadProfiles();

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
