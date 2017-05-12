/*
 * Copyright (C) 2016~2016 by CSSlayer
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

#include "chttrans.h"
#include "config.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcntl.h>
#ifdef ENABLE_OPENCC
#include "chttrans-opencc.h"
#endif
#include "chttrans-native.h"

using namespace fcitx;

static ChttransIMType inputMethodType(const InputMethodEntry &entry) {
    if (entry.languageCode() == "zh_CN") {
        return ChttransIMType::Simp;
    } else if (entry.languageCode() == "zh_HK" ||
               entry.languageCode() == "zh_TW") {
        return ChttransIMType::Trad;
    }
    return ChttransIMType::Other;
}

Chttrans::Chttrans(fcitx::Instance *instance) : instance_(instance) {
    reloadConfig();
#ifdef ENABLE_OPENCC
    backends_.emplace(ChttransEngine::OpenCC,
                      std::make_unique<OpenCCBackend>());
#endif
    backends_.emplace(ChttransEngine::Native,
                      std::make_unique<NativeBackend>());

    eventHandler_.reset(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::Default,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            if (keyEvent.isRelease()) {
                return;
            }
            auto ic = keyEvent.inputContext();
            auto engine = instance_->inputMethodEngine(ic);
            auto entry = instance_->inputMethodEntry(ic);
            if (!engine || !entry) {
                return;
            }
            auto type = inputMethodType(*entry);
            if (type == ChttransIMType::Other) {
                return;
            }
            if (keyEvent.key().checkKeyList(config_.hotkey.value())) {
                bool tradEnabled;
                if (enabledIM_.count(entry->name())) {
                    enabledIM_.erase(entry->name());
                    tradEnabled = type == ChttransIMType::Simp ? false : true;
                } else {
                    enabledIM_.insert(entry->name());
                    tradEnabled = type == ChttransIMType::Simp ? true : false;
                }
                if (notifications()) {
                    notifications()->call<INotifications::showTip>(
                        "fcitx-chttrans-toggle", "fcitx",
                        tradEnabled ? "fcitx-chttrans-active"
                                    : "fcitx-chttrans-inactive",
                        _("Simplified Chinese To Traditional Chinese"),
                        tradEnabled ? _("Traditional Chinese is enabled.")
                                    : _("Simplified Chinese is enabled."),
                        -1);
                }
                keyEvent.filterAndAccept();
            }
        }));
    outputFilterConn_ = instance_->connect<Instance::OutputFilter>([this](
        InputContext *inputContext, Text &text) {
        auto type = convertType(inputContext);
        if (type == ChttransIMType::Other) {
            return;
        }
        Text newText;
        for (size_t i = 0; i < text.size(); i++) {
            newText.append(convert(type, text.stringAt(i)), text.formatAt(i));
        }
        if (text.cursor() >= 0) {
            auto length = utf8::lengthN(text.toString(), text.cursor());
            newText.setCursor(utf8::nthChar(newText.toString(), length));
        } else {
            newText.setCursor(text.cursor());
        }
        text = std::move(newText);
    });
    commitFilterConn_ = instance_->connect<Instance::CommitFilter>(
        [this](InputContext *inputContext, std::string &str) {
            auto type = convertType(inputContext);
            if (type == ChttransIMType::Other) {
                return;
            }
            str = convert(type, str);
        });
}

void Chttrans::reloadConfig() {
    auto &standardPath = StandardPath::global();
    auto file = standardPath.open(StandardPath::Type::Config,
                                  "fcitx5/conf/chttrans.conf", O_RDONLY);
    RawConfig config;
    readFromIni(config, file.fd());

    config_.load(config);
    enabledIM_.clear();
    enabledIM_.insert(config_.enabledIM.value().begin(),
                      config_.enabledIM.value().end());
}

void Chttrans::save() {
    std::vector<std::string> values_;
    for (const auto &id : enabledIM_) {
        values_.push_back(id);
    }
    config_.enabledIM.setValue(std::move(values_));

    safeSaveAsIni(config_, "fcitx5/conf/chttrans.conf");
}

std::string Chttrans::convert(ChttransIMType type, const std::string &str) {
    auto iter = backends_.find(config_.engine.value());
    if (iter == backends_.end()) {
        iter = backends_.find(ChttransEngine::Native);
    }
    if (iter == backends_.end() || !iter->second->load()) {
        return str;
    }

    if (type == ChttransIMType::Simp) {
        return iter->second->convertSimpToTrad(str);
    } else {
        return iter->second->convertTradToSimp(str);
    }
}

ChttransIMType Chttrans::convertType(fcitx::InputContext *inputContext) {
    auto engine = instance_->inputMethodEngine(inputContext);
    auto entry = instance_->inputMethodEntry(inputContext);
    if (!engine || !entry) {
        return ChttransIMType::Other;
    }
    auto type = inputMethodType(*entry);
    if (type == ChttransIMType::Other) {
        return ChttransIMType::Other;
    }

    if (!enabledIM_.count(entry->name())) {
        return ChttransIMType::Other;
    }
    return type;
}

class ChttransModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new Chttrans(manager->instance());
    }
};

FCITX_ADDON_FACTORY(ChttransModuleFactory)
