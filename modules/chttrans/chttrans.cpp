/*
 * Copyright (C) 2016~2016 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
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
#include <fcitx/inputcontext.h>
#include <fcitx/userinterfacemanager.h>

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
    instance_->userInterfaceManager().registerAction("chttrans",
                                                     &toggleAction_);
    reloadConfig();
#ifdef ENABLE_OPENCC
    backends_.emplace(ChttransEngine::OpenCC,
                      std::make_unique<OpenCCBackend>());
#endif
    backends_.emplace(ChttransEngine::Native,
                      std::make_unique<NativeBackend>());

    eventHandler_ = instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::Default,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            if (keyEvent.isRelease()) {
                return;
            }
            auto ic = keyEvent.inputContext();
            auto engine = instance_->inputMethodEngine(ic);
            auto entry = instance_->inputMethodEntry(ic);
            if (!engine || !entry ||
                !toggleAction_.isParent(&ic->statusArea())) {
                return;
            }
            auto type = inputMethodType(*entry);
            if (type == ChttransIMType::Other) {
                return;
            }
            if (keyEvent.key().checkKeyList(config_.hotkey.value())) {
                toggle(ic);
                bool tradEnabled;
                if (enabledIM_.count(entry->uniqueName())) {
                    tradEnabled = type == ChttransIMType::Trad ? false : true;
                } else {
                    tradEnabled = type == ChttransIMType::Trad ? true : false;
                }
                if (notifications()) {
                    notifications()->call<INotifications::showTip>(
                        "fcitx-chttrans-toggle", "fcitx",
                        tradEnabled ? "fcitx-chttrans-active"
                                    : "fcitx-chttrans-inactive",
                        tradEnabled ? _("Traditional Chinese")
                                    : _("Simplified Chinese"),
                        tradEnabled ? _("Traditional Chinese is enabled.")
                                    : _("Simplified Chinese is enabled."),
                        -1);
                }
                keyEvent.filterAndAccept();
            }
        });
    outputFilterConn_ = instance_->connect<Instance::OutputFilter>([this](
        InputContext *inputContext, Text &text) {
        auto type = convertType(inputContext);
        if (type == ChttransIMType::Other || !toggleAction_.isParent(&inputContext->statusArea())) {
            return;
        }
        auto oldString = text.toString();
        auto oldLength = utf8::lengthValidated(oldString);
        if (oldLength == utf8::INVALID_LENGTH) {
            return;
        }
        auto newString = convert(type, oldString);
        auto newLength = utf8::lengthValidated(newString);
        if (newLength == utf8::INVALID_LENGTH) {
            return;
        }
        Text newText;
        size_t off = 0;
        size_t remainLength = newLength;
        for (size_t i = 0; i < text.size(); i++) {
            auto segmentLength = utf8::length(text.stringAt(i));
            if (remainLength < segmentLength) {
                segmentLength = remainLength;
            }
            remainLength -= segmentLength;
            size_t segmentByteLength =
                utf8::ncharByteLength(newString.begin() + off, segmentLength);
            newText.append(newString.substr(off, segmentByteLength),
                           text.formatAt(i));
            off = off + segmentByteLength;
        }
        if (text.cursor() >= 0) {
            auto length = utf8::length(oldString, 0, text.cursor());
            if (length > newLength) {
                length = newLength;
            }
            newText.setCursor(
                utf8::ncharByteLength(newText.toString().begin(), length));
        } else {
            newText.setCursor(text.cursor());
        }
        text = std::move(newText);
    });
    commitFilterConn_ = instance_->connect<Instance::CommitFilter>(
        [this](InputContext *inputContext, std::string &str) {
            auto type = convertType(inputContext);
            if (type == ChttransIMType::Other || !toggleAction_.isParent(&inputContext->statusArea())) {
                return;
            }
            str = convert(type, str);
        });
}

void Chttrans::toggle(InputContext *ic) {
    auto engine = instance_->inputMethodEngine(ic);
    auto entry = instance_->inputMethodEntry(ic);
    if (!engine || !entry || !toggleAction_.isParent(&ic->statusArea())) {
        return;
    }
    auto type = inputMethodType(*entry);
    if (type == ChttransIMType::Other) {
        return;
    }
    if (enabledIM_.count(entry->uniqueName())) {
        enabledIM_.erase(entry->uniqueName());
    } else {
        enabledIM_.insert(entry->uniqueName());
    }
    toggleAction_.update(ic);
}

void Chttrans::reloadConfig() {
    readAsIni(config_, "conf/chttrans.conf");
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

    safeSaveAsIni(config_, "conf/chttrans.conf");
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

    if (!enabledIM_.count(entry->uniqueName())) {
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
