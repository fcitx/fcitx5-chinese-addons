/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
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
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
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
    instance_->userInterfaceManager().registerAction("fullwidth",
                                                     &toggleAction_);
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
                setEnabled(!enabled_, keyEvent.inputContext());
                if (notifications()) {
                    notifications()->call<INotifications::showTip>(
                        "fcitx-fullwidth-toggle", _("Full width character"),
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
    commitFilterConn_ = instance_->connect<Instance::CommitFilter>(
        [this](InputContext *inputContext, std::string &str) {
            if (!enabled_ || !inWhiteList(inputContext)) {
                return;
            }
            auto len = utf8::length(str);
            std::string result;
            const auto *ps = str.c_str();
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

void Fullwidth::reloadConfig() { readAsIni(config_, "conf/fullwidth.conf"); }

bool Fullwidth::inWhiteList(InputContext *inputContext) const {
    return toggleAction_.isParent(&inputContext->statusArea());
}

class FullwidthModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new Fullwidth(manager->instance());
    }
};

FCITX_ADDON_FACTORY(FullwidthModuleFactory)
