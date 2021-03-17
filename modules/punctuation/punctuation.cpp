/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "punctuation.h"
#include "notifications_public.h"
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <string_view>
#include <unordered_set>

using namespace fcitx;

namespace {
const std::string emptyString;
const std::pair<std::string, std::string> emptyStringPair;
} // namespace

bool dontConvertWhenEn(uint32_t c) { return c == '.' || c == ','; }

std::string getLangByPath(const std::string &path) {
    if (stringutils::startsWith(path, "punctuationmap-")) {
        return path.substr(path.find('-') + 1);
    }
    return "";
}

class PunctuationState : public InputContextProperty {
public:
    std::unordered_map<uint32_t, std::string> lastPuncStack_;
    char lastIsEngOrDigit_ = 0;
    uint32_t notConverted_ = 0;
    bool mayRebuildStateFromSurroundingText_ = false;

    std::unordered_map<uint32_t, std::string> lastPuncStackBackup_;
    uint32_t notConvertedBackup_ = 0;
};

PunctuationProfile::PunctuationProfile(std::istream &in) {
    std::string strBuf;
    while (std::getline(in, strBuf)) {
        auto pair = stringutils::trimInplace(strBuf);
        std::string::size_type start = pair.first, end = pair.second;
        if (start == end) {
            continue;
        }
        std::string text(strBuf.begin() + start, strBuf.begin() + end);
        auto tokens = stringutils::split(strBuf, FCITX_WHITESPACE);
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
        decltype(puncMap_)::mapped_type p;
        p.first = tokens[1];
        if (tokens.size() > 2) {
            p.second = tokens[2];
        }
        puncMap_.emplace(c, std::move(p));
    }
}

const std::pair<std::string, std::string> &
PunctuationProfile::getPunctuation(uint32_t unicode) const {
    auto iter = puncMap_.find(unicode);
    if (iter == puncMap_.end()) {
        return emptyStringPair;
    }
    return iter->second;
}

void PunctuationProfile::setupPunctuationMapConfig() {
    auto configValue = punctuationMapConfig_.entries.mutableValue();
    configValue->clear();

    for (auto &[key, value] : puncMap_) {
        PunctuationMapEntryConfig entryConfig;
        std::string punc(1, (char)key);
        entryConfig.original.setValue(punc);
        entryConfig.mapResult1.setValue(value.first);
        entryConfig.mapResult2.setValue(value.second);
        configValue->emplace_back(entryConfig);
    }
}

PunctuationMapConfig *PunctuationProfile::getPunctuationMapConfig() {
    return &punctuationMapConfig_;
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
    auto processKeyEvent = [this](const KeyEventBase &event) {
        const auto &keyEvent = static_cast<const ForwardKeyEvent &>(event);
        auto *state = keyEvent.inputContext()->propertyFor(&factory_);
        if (keyEvent.isRelease()) {
            return;
        }
        if (!event.accepted()) {
            if (keyEvent.key().isUAZ() || keyEvent.key().isLAZ() ||
                keyEvent.key().isDigit()) {
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
                auto range =
                    utf8::MakeUTF8CharRange(MakeIterRange(text.begin(), end));
                for (auto iter = std::begin(range); iter != std::end(range);
                     iter++) {
                    auto charRange = iter.charRange();
                    std::string_view chr(
                        &*charRange.first,
                        std::distance(charRange.first, charRange.second));
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
    populateConfig(false);
}

void Punctuation::populateConfig(bool isReadSystemConfig) {
    std::map<std::string, StandardPathFile> files;
    if (isReadSystemConfig) {
        files = StandardPath::global().multiOpen(
            StandardPath::Type::PkgData, "punctuation", O_RDONLY,
            filter::Prefix("punc.mb."), filter::Not(filter::User()));
    } else {
        files = StandardPath::global().multiOpen(StandardPath::Type::PkgData,
                                                 "punctuation", O_RDONLY,
                                                 filter::Prefix("punc.mb."));
    }

    auto iter = profiles_.begin();
    while (iter != profiles_.end()) {
        if (!files.count("punc.mb." + iter->first)) {
            iter = profiles_.erase(iter);
        } else {
            iter++;
        }
    }

    for (const auto &file : files) {
        if (file.first.size() <= 8) {
            continue;
        }
        auto lang = file.first.substr(8);

        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.second.fd(),
                       boost::iostreams::file_descriptor_flags::
                           never_close_handle);
            std::istream in(&buffer);
            PunctuationProfile newProfile(in);
            profiles_[lang] = std::move(newProfile);
        } catch (const std::exception &e) {
            FCITX_LOG(Warn)
                << "Error when load profile " << file.first << ": " << e.what();
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

const fcitx::Configuration *
Punctuation::getSubConfig(const std::string &path) const {
    auto lang = getLangByPath(path);
    if (profiles_.count(lang) > 0) {
        const_cast<Punctuation *>(this)->populateConfig(true);
        const_cast<Punctuation *>(this)
            ->profiles_[lang]
            .setupPunctuationMapConfig();
        const_cast<Punctuation *>(this)
            ->profiles_[lang]
            .getPunctuationMapConfig()
            ->syncDefaultValueToCurrent();
        const_cast<Punctuation *>(this)->populateConfig(false);
        const_cast<Punctuation *>(this)
            ->profiles_[lang]
            .setupPunctuationMapConfig();
        return const_cast<Punctuation *>(this)
            ->profiles_[lang]
            .getPunctuationMapConfig();
    }
    return nullptr;
}

void Punctuation::setSubConfig(const std::string &path,
                               const fcitx::RawConfig &config) {
    std::string lang = getLangByPath(path);
    if (profiles_.count(lang) < 1) {
        FCITX_LOG(Warn) << "path must be punctuationmap-zh_CN or "
                           "punctuationmap-zh_HK or punctuationmap-zh_TW!";
        return;
    }
    std::string puncPath =
        stringutils::joinPath("punctuation", "punc.mb." + lang);
    std::unordered_map<std::string, std::string> originalMaps;
    profiles_[lang].getPunctuationMapConfig()->load(config, true);
    auto &entries = profiles_[lang].getPunctuationMapConfig()->entries.value();
    // remove duplicate entry
    for (auto &entry : entries) {
        originalMaps[entry.original.value()] =
            entry.mapResult1.value() + " " + entry.mapResult2.value();
    }
    StandardPath::global().safeSave(
        StandardPath::Type::PkgData, puncPath, [this, originalMaps](int fd) {
            for (auto &originalMap : originalMaps) {
                fs::safeWrite(fd, originalMap.first.data(),
                              originalMap.first.size());
                fs::safeWrite(fd, " ", sizeof(char));
                fs::safeWrite(fd, originalMap.second.data(),
                              originalMap.second.size());
                fs::safeWrite(fd, "\n", sizeof(char));
            }
            return true;
        });

    populateConfig(false);
    profiles_[lang].setupPunctuationMapConfig();
}

FCITX_ADDON_FACTORY(PunctuationFactory);
