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

#include "fullwidth.h"
#include "notifications_public.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcntl.h>

using namespace fcitx;

const char *sCornerTrans[] = {
    "　", "！", "＂", "＃", "￥", "％", "＆", "＇", "（", "）", "＊", "＋",
    "，", "－", "．", "／", "０", "１", "２", "３", "４", "５", "６", "７",
    "８", "９", "：", "；", "＜", "＝", "＞", "？", "＠", "Ａ", "Ｂ", "Ｃ",
    "Ｄ", "Ｅ", "Ｆ", "Ｇ", "Ｈ", "Ｉ", "Ｊ", "Ｋ", "Ｌ", "Ｍ", "Ｎ", "Ｏ",
    "Ｐ", "Ｑ", "Ｒ", "Ｓ", "Ｔ", "Ｕ", "Ｖ", "Ｗ", "Ｘ", "Ｙ", "Ｚ", "［",
    "＼", "］", "＾", "＿", "｀", "ａ", "ｂ", "ｃ", "ｄ", "ｅ", "ｆ", "ｇ",
    "ｈ", "ｉ", "ｊ", "ｋ", "ｌ", "ｍ", "ｎ", "ｏ", "ｐ", "ｑ", "ｒ", "ｓ",
    "ｔ", "ｕ", "ｖ", "ｗ", "ｘ", "ｙ", "ｚ", "｛", "｜", "｝", "～",
};

Fullwidth::Fullwidth(Instance *instance) : instance_(instance) {

    auto filterKey = [this](Event &event) {
        if (!enabled_) {
            return;
        }
        auto &keyEvent = static_cast<KeyEventBase &>(event);
        if (!inWhiteList(keyEvent.inputContext())) {
            return;
        }
        if (keyEvent.key().states() || keyEvent.isRelease()) {
            return;
        }
        auto key = static_cast<uint32_t>(keyEvent.key().sym());
        if (key >= 32 && key - 32 < FCITX_ARRAY_SIZE(sCornerTrans)) {
            keyEvent.accept();
            keyEvent.inputContext()->commitString(sCornerTrans[key - 32]);
        }
    };

    eventHandlers_.emplace_back(instance->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::Default,
        [this, filterKey](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            if (keyEvent.isRelease()) {
                return;
            }
            if (!inWhiteList(keyEvent.inputContext())) {
                return;
            }

            if (keyEvent.key().checkKeyList(config_.hotkey.value())) {
                enabled_ = !enabled_;
                if (notifications()) {
                    notifications()->call<INotifications::showTip>(
                        "fcitx-fullwidth-toggle", "fcitx",
                        enabled_ ? "fcitx-fullwidth-active"
                                 : "fcitx-fullwidth-inactive",
                        _("Full width Character"),
                        enabled_ ? _("Full width Character is enabled.")
                                 : _("Full width Character is disabled."),
                        -1);
                }
                keyEvent.filterAndAccept();
                return;
            }

            return filterKey(event);
        }));
    eventHandlers_.emplace_back(
        instance->watchEvent(EventType::InputContextForwardKey,
                             EventWatcherPhase::Default, filterKey));
    commitFilterConn_ = instance_->connect<Instance::CommitFilter>(
        [this](InputContext *inputContext, std::string &str) {
            if (!enabled_ || !inWhiteList(inputContext)) {
                return;
            }
            auto len = utf8::length(str);
            std::string result;
            auto ps = str.c_str();
            for (size_t i = 0; i < len; ++i) {
                uint32_t wc;
                char *nps;
                nps = fcitx_utf8_get_char(ps, &wc);
                int chr_len = nps - ps;
                if (wc > 32 && wc - 32 < FCITX_ARRAY_SIZE(sCornerTrans)) {
                    result.append(sCornerTrans[wc - 32]);
                } else {
                    result.append(ps, chr_len);
                }

                ps = nps;
            }
            str = std::move(result);
        });

    reloadConfig();
}

void Fullwidth::reloadConfig() {
    auto &standardPath = StandardPath::global();
    auto file = standardPath.open(StandardPath::Type::PkgConfig,
                                  "conf/fullwidth.conf", O_RDONLY);
    RawConfig config;
    readFromIni(config, file.fd());

    config_.load(config);
}

bool Fullwidth::inWhiteList(InputContext *inputContext) const {
    auto engine = instance_->inputMethodEngine(inputContext);
    auto entry = instance_->inputMethodEntry(inputContext);
    if (!engine || !entry || !whiteList_.count(entry->uniqueName())) {
        return false;
    }
    return true;
}

class FullwidthModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new Fullwidth(manager->instance());
    }
};

FCITX_ADDON_FACTORY(FullwidthModuleFactory)
