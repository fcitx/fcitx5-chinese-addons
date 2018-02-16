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
#ifndef _FULLWIDTH_FULLWIDTH_H_
#define _FULLWIDTH_FULLWIDTH_H_

#include <fcitx-config/configuration.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

FCITX_CONFIGURATION(FullWidthConfig, fcitx::Option<fcitx::KeyList> hotkey{
                                         this, "Hotkey", "Toggle key"};)

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
    void save() override;

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
    fcitx::AddonInstance *notifications_ = nullptr;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        eventHandlers_;
    fcitx::ScopedConnection commitFilterConn_;
    std::unordered_set<std::string> whiteList_;
    ToggleAction toggleAction_{this};
};

#endif // _FULLWIDTH_FULLWIDTH_H_
