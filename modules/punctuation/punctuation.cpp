/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "punctuation.h"
#include "notifications_public.h"
#include <algorithm>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <cstdint>
#include <exception>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace fcitx;

namespace {
const std::string emptyString;
const std::pair<std::string, std::string> emptyStringPair;

bool dontConvertWhenEn(uint32_t c) { return c == '.' || c == ','; }

std::string langByPath(const std::string &path) {
    constexpr std::string_view prefix = "punctuationmap/";
    if (stringutils::startsWith(path, prefix)) {
        return path.substr(prefix.size());
    }
    return "";
}
} // namespace

class PunctuationState : public InputContextProperty {
public:
    std::unordered_map<uint32_t, std::string> lastPuncStack_;
    char lastIsEngOrDigit_ = 0;
    uint32_t notConverted_ = 0;
    bool mayRebuildStateFromSurroundingText_ = false;

    std::unordered_map<uint32_t, std::string> lastPuncStackBackup_;
    uint32_t notConvertedBackup_ = 0;
};

void PunctuationProfile::loadSystem(std::istream &in) {
    load(in);
    punctuationMapConfig_.syncDefaultValueToCurrent();
}

void PunctuationProfile::resetDefaultValue() {
    punctuationMapConfig_ = PunctuationMapConfig();
    punctuationMapConfig_.syncDefaultValueToCurrent();
}

void PunctuationProfile::addEntry(uint32_t key, const std::string &value,
                                  const std::string &value2) {
    decltype(puncMap_)::mapped_type::value_type p;
    p.first = value;
    p.second = value2;
    puncMap_[key].push_back(p);

    std::string punc = utf8::UCS4ToUTF8(key);
    auto *configValue = punctuationMapConfig_.entries.mutableValue();
    configValue->emplace_back();
    PunctuationMapEntryConfig &entryConfig = configValue->back();
    entryConfig.key.setValue(punc);
    entryConfig.mapResult1.setValue(value);
    entryConfig.mapResult2.setValue(value2);
}

void PunctuationProfile::load(std::istream &in) {
    puncMap_.clear();
    auto *configValue = punctuationMapConfig_.entries.mutableValue();
    configValue->clear();

    std::string strBuf;
    while (std::getline(in, strBuf)) {
        std::string_view trimmed = stringutils::trimView(strBuf);
        if (trimmed.empty()) {
            continue;
        }
        auto tokens = stringutils::split(trimmed, FCITX_WHITESPACE);
        if (tokens.size() != 2 && tokens.size() != 3) {
            continue;
        }

        if (!std::any_of(
                tokens.begin(), tokens.end(),
                [](const std::string &s) { return utf8::validate(s); })) {
            continue;
        }
        // we don't make # as comment here, # would be consider as a valid char
        if (utf8::lengthValidated(tokens[0]) != 1) {
            continue;
        }
        auto c = utf8::getChar(tokens[0]);
        addEntry(c, tokens[1], tokens.size() > 2 ? tokens[2] : "");
    }
}

void PunctuationProfile::set(const RawConfig &config) {
    PunctuationMapConfig newConfig;
    newConfig.load(config);

    puncMap_.clear();
    auto *configValue = punctuationMapConfig_.entries.mutableValue();
    configValue->clear();

    const auto &entries = *newConfig.entries;
    // remove duplicate entry
    for (const auto &entry : entries) {
        if (entry.key->empty() || entry.mapResult1->empty()) {
            continue;
        }
        if (utf8::lengthValidated(*entry.key) != 1) {
            continue;
        }
        auto c = utf8::getChar(*entry.key);

        addEntry(c, *entry.mapResult1, *entry.mapResult2);
    }
}

void PunctuationProfile::save(std::string_view name) const {
    StandardPath::global().safeSave(
        StandardPath::Type::PkgData,
        stringutils::concat("punctuation/", profilePrefix, name),
        [this](int fd) {
            for (const auto &entry : *punctuationMapConfig_.entries) {
                fs::safeWrite(fd, entry.key->data(), entry.key->size());
                fs::safeWrite(fd, " ", sizeof(char));
                fs::safeWrite(fd, entry.mapResult1->data(),
                              entry.mapResult1->size());
                if (!entry.mapResult2->empty()) {
                    fs::safeWrite(fd, " ", sizeof(char));
                    fs::safeWrite(fd, entry.mapResult2->data(),
                                  entry.mapResult2->size());
                }
                fs::safeWrite(fd, "\n", sizeof(char));
            }
            return true;
        });
}

const std::pair<std::string, std::string> &
PunctuationProfile::getPunctuation(uint32_t unicode) const {
    auto iter = puncMap_.find(unicode);
    if (iter == puncMap_.end()) {
        return emptyStringPair;
    }
    return iter->second[0];
}

std::vector<std::string>
PunctuationProfile::getPunctuations(uint32_t unicode) const {
    auto iter = puncMap_.find(unicode);
    if (iter == puncMap_.end()) {
        return {};
    }
    // Return only first if the result size is 1.
    // This allows single paired symbol to work.
    if (iter->second.size() == 1) {
        return {iter->second[0].first};
    }
    std::vector<std::string> result;
    for (const auto &punc : iter->second) {
        result.push_back(punc.first);
        if (!punc.second.empty()) {
            result.push_back(punc.second);
        }
    }
    return result;
}

Punctuation::Punctuation(Instance *instance)
    : instance_(instance),
      factory_([](InputContext &) { return new PunctuationState; }) {
    reloadConfig();
    if (!instance_) {
        return;
    }
    instance_->inputContextManager().registerProperty("punctuationState",
                                                      &factory_);
    instance_->userInterfaceManager().registerAction("punctuation",
                                                     &toggleAction_);

    commitConn_ = instance_->connect<Instance::CommitFilter>(
        [this](InputContext *ic, const std::string &sentence) {
            auto *state = ic->propertyFor(&factory_);
            // Though sentence is utf8, we only check ascii.
            if (!sentence.empty() && (charutils::isupper(sentence.back()) ||
                                      charutils::islower(sentence.back()) ||
                                      charutils::isdigit(sentence.back()))) {
                state->lastIsEngOrDigit_ = sentence.back();
            } else {
                state->lastIsEngOrDigit_ = '\0';
            }
        });
    auto processKeyEvent = [this](const KeyEventBase &keyEvent) {
        auto *state = keyEvent.inputContext()->propertyFor(&factory_);
        if (keyEvent.isRelease()) {
            return;
        }
        if (!keyEvent.accepted()) {
            if (keyEvent.key().isUAZ() || keyEvent.key().isLAZ() ||
                keyEvent.key().isDigit() ||
                (keyEvent.key().sym() >= FcitxKey_KP_0 &&
                 keyEvent.key().sym() <= FcitxKey_KP_9 &&
                 !keyEvent.key().hasModifier())) {
                state->lastIsEngOrDigit_ =
                    Key::keySymToUnicode(keyEvent.key().sym());
            } else {
                state->lastIsEngOrDigit_ = '\0';
            }
        }
    };
    keyEventConn_ =
        instance_->connect<Instance::KeyEventResult>(processKeyEvent);
    eventWatchers_.emplace_back(instance_->watchEvent(
        EventType::InputContextForwardKey, EventWatcherPhase::PostInputMethod,
        [processKeyEvent](Event &event) {
            auto &keyEvent = static_cast<ForwardKeyEvent &>(event);
            processKeyEvent(keyEvent);
        }));
    eventWatchers_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PostInputMethod,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            if (keyEvent.isRelease()) {
                return;
            }
            if (inWhiteList(keyEvent.inputContext()) &&
                keyEvent.key().checkKeyList(config_.hotkey.value())) {
                setEnabled(!enabled(), keyEvent.inputContext());
                if (notifications()) {
                    notifications()->call<INotifications::showTip>(
                        "fcitx-punc-toggle", _("Punctuation"),
                        enabled() ? "fcitx-punc-active" : "fcitx-punc-inactive",
                        _("Punctuation"),
                        enabled() ? _("Full width punctuation is enabled.")
                                  : _("Full width punctuation is disabled."),
                        -1);
                }
                return keyEvent.filterAndAccept();
            }
        }));
    eventWatchers_.emplace_back(instance_->watchEvent(
        EventType::InputContextFocusIn, EventWatcherPhase::PostInputMethod,
        [this](Event &event) {
            auto &icEvent = static_cast<InputContextEvent &>(event);
            auto *ic = icEvent.inputContext();
            auto *state = ic->propertyFor(&factory_);
            if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                state->mayRebuildStateFromSurroundingText_ = true;
            }
        }));
    eventWatchers_.emplace_back(instance_->watchEvent(
        EventType::InputContextReset, EventWatcherPhase::PostInputMethod,
        [this](Event &event) {
            auto &icEvent = static_cast<InputContextEvent &>(event);
            auto *ic = icEvent.inputContext();
            auto *state = ic->propertyFor(&factory_);
            state->lastIsEngOrDigit_ = 0;
            // Backup the state.
            state->notConvertedBackup_ = state->notConverted_;
            state->notConverted_ = 0;
            // Backup the state.
            state->lastPuncStackBackup_ = state->lastPuncStack_;
            state->lastPuncStack_.clear();
            if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
                state->mayRebuildStateFromSurroundingText_ = true;
            }
        }));
    eventWatchers_.emplace_back(instance_->watchEvent(
        EventType::InputContextSurroundingTextUpdated,
        EventWatcherPhase::PostInputMethod, [this](Event &event) {
            auto &icEvent = static_cast<InputContextEvent &>(event);
            auto *ic = icEvent.inputContext();
            auto *state = ic->propertyFor(&factory_);
            // Enable to rebuild punctuation state from surrounding text.
            if (state->mayRebuildStateFromSurroundingText_) {
                state->mayRebuildStateFromSurroundingText_ = false;
            } else {
                state->notConvertedBackup_ = 0;
                state->lastPuncStackBackup_.clear();
                return;
            }
            if (!ic->capabilityFlags().test(CapabilityFlag::SurroundingText) ||
                !ic->surroundingText().isValid()) {
                return;
            }
            // We need text before the cursor.
            const auto &text = ic->surroundingText().text();
            auto cursor = ic->surroundingText().cursor();
            auto length = utf8::lengthValidated(text);
            if (length == utf8::INVALID_LENGTH) {
                return;
            }
            if (cursor <= 0 && cursor > length) {
                return;
            }
            uint32_t lastCharBeforeCursor;
            auto start = utf8::nextNChar(text.begin(), cursor - 1);
            auto end =
                utf8::getNextChar(start, text.end(), &lastCharBeforeCursor);
            if (lastCharBeforeCursor == utf8::INVALID_CHAR ||
                lastCharBeforeCursor == utf8::NOT_ENOUGH_SPACE) {
                return;
            }
            // Need to make sure we have ascii.
            if (std::distance(start, end) == 1 &&
                (charutils::isupper(lastCharBeforeCursor) ||
                 charutils::islower(lastCharBeforeCursor) ||
                 charutils::isdigit(lastCharBeforeCursor))) {
                state->lastIsEngOrDigit_ = lastCharBeforeCursor;
            }
            // Restore the not converted state if we still after the same chr.
            if (lastCharBeforeCursor == state->notConvertedBackup_ &&
                state->notConverted_ == 0) {
                state->notConverted_ = state->notConvertedBackup_;
            }
            state->notConvertedBackup_ = 0;
            // Scan through the surrounding text
            if (!state->lastPuncStackBackup_.empty() &&
                state->lastPuncStack_.empty()) {
                auto range = utf8::MakeUTF8StringViewRange(
                    MakeIterRange(text.begin(), end));
                for (std::string_view chr : range) {
                    auto puncIter = std::find_if(
                        state->lastPuncStackBackup_.begin(),
                        state->lastPuncStackBackup_.end(),
                        [chr](auto &p) { return p.second == chr; });
                    if (puncIter != state->lastPuncStackBackup_.end()) {
                        state->lastPuncStack_.insert(*puncIter);
                    }
                }
            }
            state->lastPuncStackBackup_.clear();
        }));
}

Punctuation::~Punctuation() {}

void Punctuation::reloadConfig() {
    readAsIni(config_, "conf/punctuation.conf");
    loadProfiles();
}

void Punctuation::loadProfiles() {
    auto systemFiles = StandardPath::global().locate(
        StandardPath::Type::PkgData, "punctuation",
        filter::Prefix(std::string(PunctuationProfile::profilePrefix)),
        filter::Not(filter::User()));
    auto allFiles = StandardPath::global().locate(
        StandardPath::Type::PkgData, "punctuation",
        filter::Prefix(std::string(PunctuationProfile::profilePrefix)));

    // Remove non-exist profiles.
    auto iter = profiles_.begin();
    while (iter != profiles_.end()) {
        if (!allFiles.count(stringutils::concat(
                PunctuationProfile::profilePrefix.size(), iter->first))) {
            iter = profiles_.erase(iter);
        } else {
            iter++;
        }
    }

    for (const auto &file : allFiles) {
        if (file.first.size() <= PunctuationProfile::profilePrefix.size()) {
            continue;
        }
        auto lang = file.first.substr(PunctuationProfile::profilePrefix.size());

        auto iter = systemFiles.find(file.first);
        const bool hasSystemFile = iter != systemFiles.end();
        bool hasUserFile = true;
        if (hasSystemFile && iter->second == file.second) {
            hasUserFile = false;
        }
        try {
            if (hasSystemFile) {
                std::ifstream in(iter->second, std::ios::in | std::ios::binary);
                profiles_[lang].loadSystem(in);
            } else {
                profiles_[lang].resetDefaultValue();
            }
            if (hasUserFile) {
                std::ifstream in(file.second, std::ios::in | std::ios::binary);
                profiles_[lang].load(in);
            }
        } catch (const std::exception &e) {
            FCITX_WARN() << "Error when load profile " << file.first << ": "
                         << e.what();
        }
    }
}

bool Punctuation::inWhiteList(fcitx::InputContext *inputContext) const {
    return toggleAction_.isParent(&inputContext->statusArea());
}

const std::string &Punctuation::pushPunctuation(const std::string &language,
                                                InputContext *ic,
                                                uint32_t unicode) {
    if (!enabled()) {
        return emptyString;
    }
    auto *state = ic->propertyFor(&factory_);
    if (state->lastIsEngOrDigit_ && *config_.halfWidthPuncAfterLatinOrNumber &&
        dontConvertWhenEn(unicode)) {
        state->notConverted_ = unicode;
        return emptyString;
    }
    auto iter = profiles_.find(language);
    if (iter == profiles_.end()) {
        return emptyString;
    }
    const auto &result = getPunctuation(language, unicode);
    state->notConverted_ = 0;
    if (result.second.empty()) {
        return result.first;
    }

    auto puncIter = state->lastPuncStack_.find(unicode);
    if (puncIter != state->lastPuncStack_.end()) {
        state->lastPuncStack_.erase(puncIter);
        return result.second;
    }
    state->lastPuncStack_.emplace(unicode, result.first);
    return result.first;
}

std::pair<std::string, std::string>
Punctuation::pushPunctuationV2(const std::string &language, InputContext *ic,
                               uint32_t unicode) {
    if (!enabled()) {
        return {emptyString, emptyString};
    }
    auto *state = ic->propertyFor(&factory_);
    if (state->lastIsEngOrDigit_ && *config_.halfWidthPuncAfterLatinOrNumber &&
        dontConvertWhenEn(unicode)) {
        state->notConverted_ = unicode;
        return {emptyString, emptyString};
    }
    auto iter = profiles_.find(language);
    if (iter == profiles_.end()) {
        return {emptyString, emptyString};
    }
    const auto &result = getPunctuation(language, unicode);
    state->notConverted_ = 0;
    if (result.second.empty()) {
        return {result.first, emptyString};
    }

    if (*config_.typePairedPunctuationTogether) {
        return {result.first, result.second};
    }

    auto puncIter = state->lastPuncStack_.find(unicode);
    if (puncIter != state->lastPuncStack_.end()) {
        state->lastPuncStack_.erase(puncIter);
        return {result.second, emptyString};
    }
    state->lastPuncStack_.emplace(unicode, result.first);
    return {result.first, emptyString};
}

const std::string &Punctuation::cancelLast(const std::string &language,
                                           InputContext *ic) {
    if (!enabled()) {
        return emptyString;
    }
    auto *state = ic->propertyFor(&factory_);
    if (dontConvertWhenEn(state->notConverted_)) {
        const auto &result = getPunctuation(language, state->notConverted_);
        state->notConverted_ = 0;
        return result.first;
    }
    return emptyString;
}

std::vector<std::string>
Punctuation::getPunctuationCandidates(const std::string &language,
                                      uint32_t unicode) {
    if (!enabled()) {
        return {};
    }
    auto iter = profiles_.find(language);
    if (iter == profiles_.end()) {
        return {};
    }
    return getPunctuations(language, unicode);
}

const std::pair<std::string, std::string> &
Punctuation::getPunctuation(const std::string &language, uint32_t unicode) {
    if (!*config_.enabled) {
        return emptyStringPair;
    }

    auto iter = profiles_.find(language);
    if (iter == profiles_.end()) {
        return emptyStringPair;
    }

    return iter->second.getPunctuation(unicode);
}

std::vector<std::string>
Punctuation::getPunctuations(const std::string &language, uint32_t unicode) {
    if (!*config_.enabled) {
        return {};
    }

    auto iter = profiles_.find(language);
    if (iter == profiles_.end()) {
        return {};
    }

    return iter->second.getPunctuations(unicode);
}

const fcitx::Configuration *
Punctuation::getSubConfig(const std::string &path) const {
    auto lang = langByPath(path);
    if (lang.empty()) {
        return nullptr;
    }
    if (auto iter = profiles_.find(lang); iter != profiles_.end()) {
        return &iter->second.config();
    }
    return nullptr;
}

void Punctuation::setSubConfig(const std::string &path,
                               const fcitx::RawConfig &config) {
    std::string lang = langByPath(path);
    auto iter = profiles_.find(lang);
    if (iter == profiles_.end()) {
        return;
    }
    iter->second.set(config);
    iter->second.save(lang);
}

FCITX_ADDON_FACTORY(PunctuationFactory);
