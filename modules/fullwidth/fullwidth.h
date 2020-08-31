/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _FULLWIDTH_FULLWIDTH_H_
#define _FULLWIDTH_FULLWIDTH_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

FCITX_CONFIGURATION(FullWidthConfig, fcitx::Option<fcitx::KeyList> hotkey{
                                         this, "Hotkey", _("Toggle key")};)

class ToggleAction;

class Fullwidth final : public fcitx::AddonInstance {
    class ToggleAction : public fcitx::Action {
    public:
        ToggleAction(Fullwidth *parent) : parent_(parent) {}

        std::string shortText(fcitx::InputContext *) const override {
            return parent_->enabled_ ? _("Full width Character")
                                     : _("Half width Character");
        }
        std::string icon(fcitx::InputContext *) const override {
            return parent_->enabled_ ? "fcitx-fullwidth-active"
                                     : "fcitx-fullwidth-inactive";
        }

        void activate(fcitx::InputContext *ic) override {
            return parent_->setEnabled(!parent_->enabled_, ic);
        }

    private:
        Fullwidth *parent_;
    };

public:
    Fullwidth(fcitx::Instance *instance);

    void reloadConfig() override;
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override {
        config_.load(config, true);
        fcitx::safeSaveAsIni(config_, "conf/fullwidth.conf");
    }

    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());

    bool inWhiteList(fcitx::InputContext *inputContext) const;

    void setEnabled(bool enabled, fcitx::InputContext *ic) {
        if (enabled != enabled_) {
            enabled_ = enabled;
            toggleAction_.update(ic);
        }
    }

private:
    bool enabled_ = false;
    fcitx::Instance *instance_;
    FullWidthConfig config_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        eventHandlers_;
    fcitx::ScopedConnection commitFilterConn_;
    std::unordered_set<std::string> whiteList_;
    ToggleAction toggleAction_{this};
};

#endif // _FULLWIDTH_FULLWIDTH_H_
