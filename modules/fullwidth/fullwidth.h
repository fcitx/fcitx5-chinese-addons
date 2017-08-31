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
#ifndef _FULLWIDTH_FULLWIDTH_H_
#define _FULLWIDTH_FULLWIDTH_H_

#include "fullwidth_public.h"
#include <fcitx-config/configuration.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

FCITX_CONFIGURATION(FullWidthConfig, fcitx::Option<fcitx::KeyList> hotkey{
                                         this, "Hotkey", "Toggle key"};)

class Fullwidth final : public fcitx::AddonInstance {
public:
    Fullwidth(fcitx::Instance *instance);

    void reloadConfig() override;

    fcitx::AddonInstance *notifications() {
        if (!notifications_) {
            notifications_ =
                instance_->addonManager().addon("notifications", true);
        }
        return notifications_;
    }

    void enable(const std::string &im) { whiteList_.insert(im); }
    void disable(const std::string &im) { whiteList_.erase(im); }

    bool inWhiteList(fcitx::InputContext *inputContext) const;

    FCITX_ADDON_EXPORT_FUNCTION(Fullwidth, enable);
    FCITX_ADDON_EXPORT_FUNCTION(Fullwidth, disable);

private:
    bool enabled_ = false;
    fcitx::Instance *instance_;
    FullWidthConfig config_;
    fcitx::AddonInstance *notifications_ = nullptr;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
        eventHandlers_;
    fcitx::ScopedConnection commitFilterConn_;
    std::unordered_set<std::string> whiteList_;
};

#endif // _FULLWIDTH_FULLWIDTH_H_
