/*
 * SPDX-FileCopyrightText: 2016-2016 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "chttrans.h"
#include "chttrans-native.h"
#include "config.h"
#include <cassert>
#include <cstddef>
#include <exception>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef ENABLE_OPENCC
#include "chttrans-opencc.h"
#ifdef HAS_BOOST_JSON
#include <boost/json/parse.hpp>
#include <boost/json/src.hpp>
#include <boost/json/value_to.hpp>
#endif
#endif

using namespace fcitx;

std::unordered_map<std::string, std::string> openCCBuiltInPath() {
    std::unordered_map<std::string, std::string> result;
    std::string prefix;
#ifdef OPENCC_PREFIX
    prefix = OPENCC_PREFIX;
#endif
    if (!prefix.empty()) {
        result["datadir"] = stringutils::joinPath(prefix, "share");
        result["pkgdatadir"] = stringutils::joinPath(prefix, "share/opencc");
    }
    return result;
}

const StandardPath &openCCStandardPath() {
    static const StandardPath standardPath(
        "opencc", openCCBuiltInPath(), StandardPath::global().skipBuiltInPath(),
        StandardPath::global().skipUserPath());
    return standardPath;
}

static ChttransIMType inputMethodEntryType(const InputMethodEntry &entry) {
    if (entry.languageCode() == "zh_CN") {
        return ChttransIMType::Simp;
    }
    if (entry.languageCode() == "zh_HK" || entry.languageCode() == "zh_TW") {
        return ChttransIMType::Trad;
    }
    return ChttransIMType::Other;
}

Chttrans::Chttrans(fcitx::Instance *instance) : instance_(instance) {
    instance_->userInterfaceManager().registerAction("chttrans",
                                                     &toggleAction_);
#ifdef ENABLE_OPENCC
    backends_.emplace(ChttransEngine::OpenCC,
                      std::make_unique<OpenCCBackend>());
#endif
    backends_.emplace(ChttransEngine::Native,
                      std::make_unique<NativeBackend>());
    reloadConfig();

    eventHandler_ = instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::Default,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            if (keyEvent.isRelease()) {
                return;
            }
            auto *ic = keyEvent.inputContext();
            if (!toggleAction_.isParent(&ic->statusArea())) {
                return;
            }
            auto type = currentType(ic);
            if (type == ChttransIMType::Other) {
                return;
            }
            if (keyEvent.key().checkKeyList(config_.hotkey.value())) {
                toggle(ic);
                // Note that type is old value before toggle, we simply try to
                // avoid recalculation.
                bool isTraditional = (type != ChttransIMType::Trad);
                if (notifications()) {
                    notifications()->call<INotifications::showTip>(
                        "fcitx-chttrans-toggle",
                        _("Simplified and Traditional Chinese Translation"),
                        isTraditional ? "fcitx-chttrans-active"
                                      : "fcitx-chttrans-inactive",
                        isTraditional ? _("Switch to Traditional Chinese")
                                      : _("Switch to Simplified Chinese"),
                        isTraditional ? _("Traditional Chinese is enabled.")
                                      : _("Simplified Chinese is enabled."),
                        1000);
                }
                keyEvent.filterAndAccept();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            }
        });
    outputFilterConn_ = instance_->connect<Instance::OutputFilter>(
        [this](InputContext *inputContext, Text &text) {
            // Short cut for empty string.
            if (text.empty()) {
                return;
            }
            if (!toggleAction_.isParent(&inputContext->statusArea())) {
                return;
            }
            auto type = convertType(inputContext);
            if (type == ChttransIMType::Other) {
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
            // Short cut for most common case, the text contains only one
            // string.
            if (text.size() == 1) {
                newText.append(std::move(newString), text.formatAt(0));
            } else {
                size_t off = 0;
                size_t remainLength = newLength;
                for (size_t i = 0; i < text.size(); i++) {
                    auto segmentLength = utf8::length(text.stringAt(i));
                    if (remainLength < segmentLength) {
                        segmentLength = remainLength;
                    }
                    remainLength -= segmentLength;
                    size_t segmentByteLength = utf8::ncharByteLength(
                        newString.begin() + off, segmentLength);
                    newText.append(newString.substr(off, segmentByteLength),
                                   text.formatAt(i));
                    off = off + segmentByteLength;
                }
            }
            if (text.cursor() > 0) {
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
            if (!toggleAction_.isParent(&inputContext->statusArea())) {
                return;
            }
            auto type = convertType(inputContext);
            if (type == ChttransIMType::Other) {
                return;
            }
            str = convert(type, str);
        });
}

void Chttrans::toggle(InputContext *ic) {
    if (!toggleAction_.isParent(&ic->statusArea())) {
        return;
    }
    auto type = inputMethodType(ic);
    if (type == ChttransIMType::Other) {
        return;
    }
    const auto *entry = instance_->inputMethodEntry(ic);
    if (enabledIM_.count(entry->uniqueName())) {
        enabledIM_.erase(entry->uniqueName());
    } else {
        enabledIM_.insert(entry->uniqueName());
    }
    syncToConfig();
    toggleAction_.update(ic);
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    ic->updatePreedit();
}

void Chttrans::reloadConfig() {
    readAsIni(config_, "conf/chttrans.conf");
    populateConfig();
}

void Chttrans::populateConfig() {
    enabledIM_.clear();
    enabledIM_.insert(config_.enabledIM.value().begin(),
                      config_.enabledIM.value().end());
    for (const auto &backend : backends_) {
        if (backend.second->loaded()) {
            backend.second->updateConfig(config_);
        }
    }
#ifdef ENABLE_OPENCC
    auto engine = config_.engine.value();
#else
    auto engine = ChttransEngine::Native;
#endif

    auto iter = backends_.find(engine);
    if (iter == backends_.end() && engine != ChttransEngine::Native) {
        iter = backends_.find(ChttransEngine::Native);
    }
    if (iter == backends_.end()) {
        currentBackend_ = nullptr;
    } else {
        currentBackend_ = iter->second.get();
    }
}

void Chttrans::syncToConfig() {
    std::vector<std::string> values_;
    values_.reserve(enabledIM_.size());
    for (const auto &id : enabledIM_) {
        values_.push_back(id);
    }
    config_.enabledIM.setValue(std::move(values_));
}

void Chttrans::save() {
    syncToConfig();
    safeSaveAsIni(config_, "conf/chttrans.conf");
}

const Configuration *Chttrans::getConfig() const {
#ifdef ENABLE_OPENCC
    std::vector<std::pair<std::string, std::string>> profiles{
        {"default", _("Default")}};
    constexpr std::string_view JsonSuffix = ".json";
    auto files =
        openCCStandardPath().locate(StandardPath::Type::PkgData, ".",
                                    filter::Suffix(std::string(JsonSuffix)));
    profiles.reserve(files.size() + 1);
    // files is std::map, so file name is already sorted.
    for (const auto &file : files) {
#ifdef HAS_BOOST_JSON
        try {
            std::ifstream in(file.second, std::ios::in | std::ios::binary);
            std::string strBuf(std::istreambuf_iterator<char>(in), {});
            auto jv = boost::json::parse(strBuf);
            const auto &obj = jv.as_object();
            auto name = boost::json::value_to<std::string>(obj.at("name"));
            std::string description =
                file.first.substr(0, file.first.size() - JsonSuffix.size());
            if (!name.empty()) {
                description = fmt::format("{0}: {1}", description,
                                          C_("OpenCC Profile", name));
            }
            profiles.emplace_back(file.first, std::move(description));
        } catch (const std::exception &e) {
            FCITX_WARN() << "Failed to parse " << file.first;
            profiles.emplace_back(file.first, file.first);
        }
#else
        profiles.emplace_back(file.first, file.first);
#endif
    }
    config_.openCCS2TProfile.annotation().setProfiles(profiles);
    config_.openCCT2SProfile.annotation().setProfiles(std::move(profiles));
#endif
    return &config_;
}

std::string Chttrans::convert(ChttransIMType type, const std::string &str) {
    if (!currentBackend_ || !currentBackend_->load(config_)) {
        return str;
    }

    if (type == ChttransIMType::Trad) {
        return currentBackend_->convertSimpToTrad(str);
    }
    return currentBackend_->convertTradToSimp(str);
}

ChttransIMType
Chttrans::inputMethodType(fcitx::InputContext *inputContext) const {
    auto *engine = instance_->inputMethodEngine(inputContext);
    const auto *entry = instance_->inputMethodEntry(inputContext);
    if (!engine || !entry) {
        return ChttransIMType::Other;
    }
    return inputMethodEntryType(*entry);
}

ChttransIMType Chttrans::convertType(fcitx::InputContext *inputContext) const {
    auto type = inputMethodType(inputContext);
    if (type == ChttransIMType::Other) {
        return ChttransIMType::Other;
    }

    const auto *entry = instance_->inputMethodEntry(inputContext);
    assert(entry);
    if (!enabledIM_.count(entry->uniqueName())) {
        return ChttransIMType::Other;
    }

    return type == ChttransIMType::Simp ? ChttransIMType::Trad
                                        : ChttransIMType::Simp;
}

ChttransIMType Chttrans::currentType(fcitx::InputContext *inputContext) const {
    auto type = inputMethodType(inputContext);
    if (type == ChttransIMType::Other) {
        return ChttransIMType::Other;
    }

    const auto *entry = instance_->inputMethodEntry(inputContext);
    assert(entry);
    if (!enabledIM_.count(entry->uniqueName())) {
        return type;
    }

    return type == ChttransIMType::Simp ? ChttransIMType::Trad
                                        : ChttransIMType::Simp;
}

class ChttransModuleFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new Chttrans(manager->instance());
    }
};

FCITX_ADDON_FACTORY(ChttransModuleFactory)
