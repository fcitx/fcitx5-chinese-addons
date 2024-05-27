/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "pinyin.h"

// Use relative path so we don't need import export target.
// We want to keep cloudpinyin logic but don't call it.
#include "../../modules/cloudpinyin/cloudpinyin_public.h"
#include "config.h"
#include "customphrase.h"
#include "pinyincandidate.h"
#include "workerthread.h"
#include <algorithm>
#include <boost/iostreams/stream_buffer.hpp>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <exception>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addoninstance.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fmt/core.h>
#include <fstream>
#include <functional>
#include <future>
#include <istream>
#include <iterator>
#include <libime/core/languagemodel.h>
#include <libime/core/triedictionary.h>
#include <libime/pinyin/pinyincorrectionprofile.h>
#include <libime/pinyin/pinyinime.h>
#include <list>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef FCITX_HAS_LUA
#include "luaaddon_public.h"
#endif
#include "notifications_public.h"
#include "pinyinhelper_public.h"
#include "punctuation_public.h"
#include "spell_public.h"
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <libime/core/historybigram.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/pinyinprediction.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <quickphrase_public.h>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(pinyin, "pinyin");

#define PINYIN_DEBUG() FCITX_LOGC(pinyin, Debug)
#define PINYIN_ERROR() FCITX_LOGC(pinyin, Error)

bool consumePrefix(std::string_view &view, std::string_view prefix) {
    if (stringutils::startsWith(view, prefix)) {
        view.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

template <typename T>
std::unique_ptr<CandidateList>
predictCandidateList(PinyinEngine *engine, const std::vector<T> &words) {
    if (words.empty()) {
        return nullptr;
    }
    auto candidateList = std::make_unique<CommonCandidateList>();
    for (const auto &word : words) {
        if constexpr (std::is_same_v<T, std::string>) {
            candidateList->append<PinyinPredictCandidateWord>(engine, word);
        } else if constexpr (std::is_same_v<
                                 T,
                                 std::pair<std::string,
                                           libime::PinyinPredictionSource>>) {

            if (word.second == libime::PinyinPredictionSource::Model) {
                candidateList->append<PinyinPredictCandidateWord>(engine,
                                                                  word.first);
            } else if (word.second ==
                       libime::PinyinPredictionSource::Dictionary) {
                candidateList->append<PinyinPredictDictCandidateWord>(
                    engine, word.first);
            }
        }
    }
    candidateList->setSelectionKey(engine->selectionKeys());
    candidateList->setPageSize(*engine->config().pageSize);
    if (!candidateList->empty()) {
        candidateList->setGlobalCursorIndex(0);
    }
    return candidateList;
}

PinyinState::PinyinState(PinyinEngine *engine) : context_(engine->ime()) {
    context_.setMaxSentenceLength(35);
}

void PinyinEngine::initPredict(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto *state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    auto lmState = context.state();
    state->predictWords_ = context.selectedWords();
    auto words =
        prediction_.predict(lmState, context.selectedWords(),
                            context.selectedWordsWithPinyin().back().second,
                            *config_.predictionSize);
    if (auto candidateList = predictCandidateList(this, words)) {
        auto &inputPanel = inputContext->inputPanel();
        inputPanel.setCandidateList(std::move(candidateList));
    } else {
        state->predictWords_.reset();
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::updatePredict(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto *state = inputContext->propertyFor(&factory_);
    assert(state->predictWords_.has_value());
    auto words =
        prediction_.predict(*state->predictWords_, *config_.predictionSize);
    if (auto candidateList = predictCandidateList(this, words)) {
        auto &inputPanel = inputContext->inputPanel();
        inputPanel.setCandidateList(std::move(candidateList));
    } else {
        // Clear if we can't do predict.
        // This help other code to detect whether we are in predict.
        state->predictWords_.reset();
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

int englishNess(const std::string &input, bool sp) {
    const auto pys = stringutils::split(input, " ");
    constexpr int fullWeight = -2;
    constexpr int shortWeight = 3;
    constexpr int invalidWeight = 6;
    constexpr int defaultWeight = shortWeight;
    int weight = 0;
    for (const auto &py : pys) {
        if (sp) {
            if (py.size() == 2) {
                weight += fullWeight / 2;
            } else {
                weight += invalidWeight;
            }
        } else {
            if (py == "ng") {
                weight += fullWeight;
            } else {
                auto firstChr = py[0];
                if (firstChr == '\'') {
                    return 0;
                }
                if (firstChr == 'i' || firstChr == 'u' || firstChr == 'v') {
                    weight += invalidWeight;
                } else if (py.size() <= 2) {
                    weight += shortWeight;
                } else if (py.find_first_of("aeiou") != std::string::npos) {
                    weight += fullWeight;
                } else {
                    weight += defaultWeight;
                }
            }
        }
    }

    if (weight < 0) {
        return 0;
    }
    return (weight + 7) / 10;
}

bool isStroke(const std::string &input) {
    static const std::unordered_set<char> py{'h', 'p', 's', 'z', 'n'};
    return std::all_of(input.begin(), input.end(),
                       [](char c) { return py.count(c); });
}

#ifdef FCITX_HAS_LUA
std::vector<std::string>
PinyinEngine::luaCandidateTrigger(InputContext *ic,
                                  const std::string &candidateString) {
    std::vector<std::string> result;
    RawConfig arg;
    arg.setValue(candidateString);
    auto ret = imeapi()->call<ILuaAddon::invokeLuaFunction>(
        ic, "candidateTrigger", arg);
    const auto *length = ret.valueByPath("Length");
    try {
        if (length) {
            auto n = std::stoi(*length);
            for (int i = 0; i < n; i++) {
                const auto *candidate = ret.valueByPath(std::to_string(i));
                if (candidate && !candidate->empty()) {
                    result.push_back(*candidate);
                }
            }
        }
    } catch (...) {
    }
    return result;
}
#endif
std::pair<Text, Text> PinyinEngine::preedit(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&factory_);
    PreeditMode mode =
        inputContext->capabilityFlags().test(CapabilityFlag::Preedit)
            ? *config_.preeditMode
            : PreeditMode::No;
    // Use const ref to avoid accidentally change anything.
    const auto &context = state->context_;
    auto preeditWithCursor = context.preeditWithCursor();
    // client preedit can be empty/pinyin/preview depends on config
    Text clientPreedit;
    Text preedit;
    switch (mode) {
    case PreeditMode::ComposingPinyin:
        if (*config_.preeditCursorPositionAtBeginning) {
            clientPreedit.append(
                preeditWithCursor.first.substr(0, preeditWithCursor.second),
                {TextFormatFlag::HighLight, TextFormatFlag::Underline});
            clientPreedit.append(
                preeditWithCursor.first.substr(preeditWithCursor.second),
                TextFormatFlag::Underline);
            clientPreedit.setCursor(0);
        } else {
            clientPreedit.append(preeditWithCursor.first,
                                 TextFormatFlag::Underline);
            clientPreedit.setCursor(preeditWithCursor.second);
        }
        break;
    case PreeditMode::CommitPreview:
        clientPreedit.append(context.sentence(), TextFormatFlag::Underline);
        if (*config_.preeditCursorPositionAtBeginning) {
            clientPreedit.setCursor(0);
        } else {
            clientPreedit.setCursor(context.selectedSentence().size());
        }
        [[fallthrough]];
    case PreeditMode::No:
        preedit.append(preeditWithCursor.first);
        preedit.setCursor(preeditWithCursor.second);
        break;
    }
    return {std::move(clientPreedit), std::move(preedit)};
}

std::string
PinyinEngine::preeditCommitString(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&factory_);
    // Use const ref to avoid accidentally change anything.
    const auto &context = state->context_;

    const auto &userInput = context.userInput();
    const auto selectedLength = context.selectedLength();

    return context.selectedSentence() +
           userInput.substr(selectedLength,
                            userInput.length() - selectedLength);
}

void PinyinEngine::updatePreedit(InputContext *inputContext) const {
    auto &inputPanel = inputContext->inputPanel();
    auto [clientPreedit, preedit] = this->preedit(inputContext);
    inputPanel.setClientPreedit(clientPreedit);
    inputPanel.setPreedit(preedit);
}

void PinyinEngine::updatePuncPreedit(InputContext *inputContext) const {
    auto candidateList = inputContext->inputPanel().candidateList();

    if (inputContext->capabilityFlags().test(CapabilityFlag::Preedit)) {
        if (candidateList->cursorIndex() >= 0) {
            Text preedit;

            const auto &candidate =
                candidateList->candidate(candidateList->cursorIndex());
            if (const auto *puncCandidate =
                    dynamic_cast<const PinyinPunctuationCandidateWord *>(
                        &candidate)) {
                preedit.append(puncCandidate->word());
            }

            preedit.setCursor(preedit.textLength());
            inputContext->inputPanel().setClientPreedit(preedit);
        }
        inputContext->updatePreedit();
    }
}

void PinyinEngine::updatePuncCandidate(
    InputContext *inputContext, const std::string &original,
    const std::vector<std::string> &candidates) const {
    inputContext->inputPanel().reset();
    auto *state = inputContext->propertyFor(&factory_);
    auto puncCandidateList = std::make_unique<CommonCandidateList>();
    puncCandidateList->setPageSize(*config_.pageSize);
    puncCandidateList->setCursorPositionAfterPaging(
        CursorPositionAfterPaging::ResetToFirst);
    for (const auto &result : candidates) {
        puncCandidateList->append<PinyinPunctuationCandidateWord>(
            this, result, original == result);
    }
    puncCandidateList->setCursorIncludeUnselected(false);
    puncCandidateList->setCursorKeepInSamePage(false);
    puncCandidateList->setGlobalCursorIndex(0);
    puncCandidateList->setSelectionKey(selectionKeys_);
    state->mode_ = PinyinMode::Punctuation;
    inputContext->inputPanel().setCandidateList(std::move(puncCandidateList));
    updatePuncPreedit(inputContext);
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::updateUI(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto *state = inputContext->propertyFor(&factory_);
    // Use const ref to avoid accidentally change anything.
    const auto &context = state->context_;
    if (context.selected()) {
        auto sentence = context.sentence();
        if (!inputContext->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive)) {
            state->context_.learn();
        }
        inputContext->commitString(sentence);
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        if (*config_.predictionEnabled) {
            initPredict(inputContext);
        }
        state->context_.clear();
        return;
    }

    do {
        if (context.userInput().empty()) {
            break;
        }
        // Update Preedit.
        updatePreedit(inputContext);
        auto &inputPanel = inputContext->inputPanel();
        // Update candidate
        const auto &candidates = context.candidatesToCursor();
        if (candidates.empty()) {
            break;
        }

        // Setup candidate list.
        auto candidateList = std::make_unique<CommonCandidateList>();
        candidateList->setPageSize(*config_.pageSize);
        candidateList->setCursorPositionAfterPaging(
            CursorPositionAfterPaging::ResetToFirst);

        // Precompute some values.
        const auto selectedLength = context.selectedLength();
        const auto selectedSentence = context.selectedSentence();

        const bool fullResult = (context.cursor() == context.size() ||
                                 context.cursor() == context.selectedLength());

        const auto pyLength = context.cursor() > selectedLength
                                  ? context.cursor() - selectedLength
                                  : std::string::npos;
        const auto pyBeforeCursor =
            context.userInput().substr(selectedLength, pyLength);

        std::list<std::unique_ptr<PinyinAbstractExtraCandidateWordInterface>>
            extraCandidates;
        std::unordered_set<std::string> customCandidateSet;
        /// Create custom phrase candidate {{{
        do {
            const auto *results = customPhrase_.lookup(pyBeforeCursor);
            if (!results) {
                break;
            }
            for (const auto &result : *results) {
                if (result.order() <= 0) {
                    continue;
                }
                auto phrase =
                    result.evaluate([this, inputContext](std::string_view key) {
                        return evaluateCustomPhrase(inputContext, key);
                    });
                if (customCandidateSet.count(phrase)) {
                    continue;
                }
                customCandidateSet.insert(phrase);
                std::string customPhrase =
                    result.isDynamic() ? result.value() : phrase;
                extraCandidates.push_back(
                    std::make_unique<CustomPhraseCandidateWord>(
                        this, result.order() - 1, pyBeforeCursor.size(),
                        std::move(phrase), std::move(customPhrase)));
            }
        } while (0);
        /// }}}

        /// Create cloud candidate. {{{
        std::optional<decltype(extraCandidates)::iterator> cloud;
        if (*config_.cloudPinyinEnabled && cloudpinyin() &&
            !inputContext->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive) &&
            fullResult) {
            using namespace std::placeholders;
            auto fullPinyin = context.useShuangpin()
                                  ? context.candidateFullPinyin(0)
                                  : context.userInput().substr(selectedLength);
            auto cand = std::make_unique<CustomCloudPinyinCandidateWord>(
                this, fullPinyin, selectedSentence, inputContext,
                [this](InputContext *ic, const std::string &selected,
                       const std::string &word) {
                    cloudPinyinSelected(ic, selected, word);
                },
                *config_.cloudPinyinIndex - 1);
            if (!cand->filled() ||
                (!cand->word().empty() &&
                 !customCandidateSet.count(cand->word()) &&
                 !context.candidatesToCursorSet().count(cand->word()))) {
                customCandidateSet.insert(cand->word());
                extraCandidates.push_back(std::move(cand));
                cloud = std::prev(extraCandidates.end());
            }
        }
        /// }}}

        /// Create spell candidate {{{
        auto [parsedPy, parsedPyCursor] = state->context_.preeditWithCursor(
            libime::PinyinPreeditMode::RawText);
        if (*config_.spellEnabled && spell() &&
            parsedPyCursor >= selectedSentence.size() &&
            selectedLength <= context.cursor()) {
            int engNess = englishNess(parsedPy, context.useShuangpin());
            if (engNess) {
                parsedPyCursor -= selectedSentence.length();
                parsedPy = parsedPy.substr(
                    selectedSentence.size(),
                    parsedPyCursor > selectedSentence.length()
                        ? parsedPyCursor - selectedSentence.length()
                        : std::string::npos);
                auto results = spell()->call<ISpell::hintWithProvider>(
                    "en", SpellProvider::Custom, pyBeforeCursor, engNess);
                std::string bestSentence;
                if (!candidates.empty()) {
                    bestSentence = candidates[0].toString();
                }
                int position = 1;
                for (auto &result : results) {
                    if (customCandidateSet.count(result) ||
                        context.candidatesToCursorSet().count(result)) {
                        continue;
                    }
                    extraCandidates.push_back(
                        std::make_unique<SpellCandidateWord>(
                            this, result, pyBeforeCursor.size(), position++));
                }
            }
        }
        /// }}}

        /// Create stroke candidate {{{
        if (pinyinhelper() && context.selectedLength() == 0 &&
            isStroke(context.userInput())) {
            int limit = (context.userInput().size() + 4) / 5;
            if (limit > 3) {
                limit = 3;
            }
            auto results = pinyinhelper()->call<IPinyinHelper::lookupStroke>(
                context.userInput(), limit);

            int strokeCandsPos =
                *config_.pageSize - static_cast<int>(results.size());
            if (strokeCandsPos < 0) {
                strokeCandsPos = *config_.pageSize - 1;
            }
            for (auto &result : results) {
                utf8::getChar(result.first);
                auto py = pinyinhelper()->call<IPinyinHelper::lookup>(
                    utf8::getChar(result.first));
                auto pystr = stringutils::join(py, " ");
                extraCandidates.push_back(std::make_unique<StrokeCandidateWord>(
                    this, result.first, pystr, strokeCandsPos++));
            }
        }
        /// }}}

        // We expect stable sort here.
        extraCandidates.sort([](const auto &lhs, const auto &rhs) {
            return lhs->order() < rhs->order();
        });

        auto maybeApplyExtraCandidates = [&extraCandidates, &candidateList,
                                          &cloud, this](bool force) {
            if (extraCandidates.empty()) {
                return;
            }
            if (candidateList->totalSize() > extraCandidates.back()->order() ||
                candidateList->totalSize() > 2 * (*config_.pageSize) || force) {
                // Since we will insert cloud pinyin, reset the iterator.
                cloud.reset();
                int lastPos = -1;
                for (auto &extraCandidate : extraCandidates) {
                    int actualPos = (lastPos >= extraCandidate->order())
                                        ? lastPos
                                        : extraCandidate->order();
                    if (actualPos > candidateList->totalSize()) {
                        actualPos = candidateList->totalSize();
                    }
                    // Rewrap it into CandidateWord.
                    std::unique_ptr<CandidateWord> cand(
                        &extraCandidate.release()->toCandidateWord());
                    candidateList->insert(actualPos, std::move(cand));
                    lastPos = actualPos;
                }
                extraCandidates.clear();
            }
        };

        for (size_t idx = 0, end = candidates.size(); idx < end; idx++) {
            const auto &candidate = candidates[idx];
            auto candidateString = candidate.toString();
            if (customCandidateSet.count(candidateString)) {
                continue;
            }
            std::vector<std::string> luaExtraCandidates;
#ifdef FCITX_HAS_LUA
            // To invoke lua trigger, we need "raw full sentence". Also, check
            // against nbest, otherwise single char may be invoked for too much
            // times.
            if (selectedLength == 0 &&
                static_cast<int>(idx) <
                    std::max(*config_.nbest, *config_.pageSize) &&
                imeapi() && fullResult &&
                candidate.sentence().back()->to()->index() ==
                    context.userInput().size()) {
                luaExtraCandidates =
                    luaCandidateTrigger(inputContext, candidateString);
            }
#endif

            // use candidateString before it is moved.
            const std::vector<std::string> *symbols = nullptr;
            if (*config_.symbolsEnabled) {
                symbols = symbols_.lookup(candidateString);
            }
            candidateList->append<PinyinCandidateWord>(
                this, inputContext, Text(std::move(candidateString)), idx);
            for (auto &extraCandidate : luaExtraCandidates) {
                candidateList->append<LuaCandidateWord>(
                    this, std::move(extraCandidate));
            }

            if (symbols) {
                for (const auto &symbol : *symbols) {
                    const bool isFull =
                        candidate.sentence().back()->to()->index() ==
                        pyBeforeCursor.size();
                    candidateList->append<SymbolCandidateWord>(
                        this, symbol, candidate, isFull);
                }
            }

            maybeApplyExtraCandidates(false);
        }

        maybeApplyExtraCandidates(true);
        candidateList->setSelectionKey(selectionKeys_);
        if (!candidateList->empty()) {
            candidateList->setGlobalCursorIndex(0);
        }
        candidateList->setActionableImpl(
            std::make_unique<PinyinActionableCandidateList>(this,
                                                            inputContext));
        inputPanel.setCandidateList(std::move(candidateList));
    } while (0);
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

std::string PinyinEngine::evaluateCustomPhrase(InputContext *inputContext,
                                               std::string_view key) {
    FCITX_UNUSED(inputContext);
    auto result = CustomPhrase::builtinEvaluator(key);
    if (!result.empty()) {
        return result;
    }

#ifdef FCITX_HAS_LUA
    if (stringutils::startsWith(key, "lua:")) {
        RawConfig config;
        auto ret = imeapi()->call<ILuaAddon::invokeLuaFunction>(
            inputContext, std::string(key.substr(4)), config);
        if (!ret.value().empty()) {
            return ret.value();
        }
    }
#endif
    return "";
}

PinyinEngine::PinyinEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &) { return new PinyinState(this); }),
      worker_(instance->eventDispatcher()) {
    ime_ = std::make_unique<libime::PinyinIME>(
        std::make_unique<libime::PinyinDictionary>(),
        std::make_unique<libime::UserLanguageModel>(
            libime::DefaultLanguageModelResolver::instance()
                .languageModelFileForLanguage("zh_CN")));

    const auto &standardPath = StandardPath::global();
    auto systemDictFile =
        standardPath.open(StandardPath::Type::Data, "libime/sc.dict", O_RDONLY);
    if (systemDictFile.isValid()) {
        boost::iostreams::stream_buffer<
            boost::iostreams::file_descriptor_source>
            buffer(systemDictFile.fd(),
                   boost::iostreams::file_descriptor_flags::never_close_handle);
        std::istream in(&buffer);
        ime_->dict()->load(libime::PinyinDictionary::SystemDict, in,
                           libime::PinyinDictFormat::Binary);
    } else {
        ime_->dict()->load(libime::PinyinDictionary::SystemDict,
                           LIBIME_INSTALL_PKGDATADIR "/sc.dict",
                           libime::PinyinDictFormat::Binary);
    }
    prediction_.setUserLanguageModel(ime_->model());
    prediction_.setPinyinDictionary(ime_->dict());

    do {
        auto file = standardPath.openUser(StandardPath::Type::PkgData,
                                          "pinyin/user.dict", O_RDONLY);

        if (file.fd() < 0) {
            break;
        }

        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                      never_close_handle);
            std::istream in(&buffer);
            ime_->dict()->load(libime::PinyinDictionary::UserDict, in,
                               libime::PinyinDictFormat::Binary);
        } catch (const std::exception &e) {
            PINYIN_ERROR() << "Failed to load pinyin dict: " << e.what();
        }
    } while (0);
    do {
        auto file = standardPath.openUser(StandardPath::Type::PkgData,
                                          "pinyin/user.history", O_RDONLY);

        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                      never_close_handle);
            std::istream in(&buffer);
            ime_->model()->load(in);
        } catch (const std::exception &e) {
            PINYIN_ERROR() << "Failed to load pinyin history: " << e.what();
        }
    } while (0);

    ime_->setScoreFilter(1);
    loadBuiltInDict();
    reloadConfig();
    loadExtraDict();
    loadCustomPhrase();
    instance_->inputContextManager().registerProperty("pinyinState", &factory_);
    KeySym syms[] = {
        FcitxKey_1, FcitxKey_2, FcitxKey_3, FcitxKey_4, FcitxKey_5,
        FcitxKey_6, FcitxKey_7, FcitxKey_8, FcitxKey_9, FcitxKey_0,
    };

    KeyStates states;
    for (auto sym : syms) {
        selectionKeys_.emplace_back(sym, states);
    }

    KeySym numpadsyms[] = {
        FcitxKey_KP_1, FcitxKey_KP_2, FcitxKey_KP_3, FcitxKey_KP_4,
        FcitxKey_KP_5, FcitxKey_KP_6, FcitxKey_KP_7, FcitxKey_KP_8,
        FcitxKey_KP_9, FcitxKey_KP_0,
    };

    KeyStates numpadStates;
    for (auto sym : numpadsyms) {
        numpadSelectionKeys_.emplace_back(sym, numpadStates);
    }

    predictionAction_.setShortText(*config_.predictionEnabled
                                       ? _("Prediction Enabled")
                                       : _("Prediction Disabled"));
    predictionAction_.setLongText(_("Show prediction words"));
    predictionAction_.setIcon(*config_.predictionEnabled
                                  ? "fcitx-remind-active"
                                  : "fcitx-remind-inactive");
    predictionAction_.connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.predictionEnabled.setValue(!(*config_.predictionEnabled));
            predictionAction_.setShortText(*config_.predictionEnabled
                                               ? _("Prediction Enabled")
                                               : _("Prediction Disabled"));
            predictionAction_.setIcon(*config_.predictionEnabled
                                          ? "fcitx-remind-active"
                                          : "fcitx-remind-inactive");
            predictionAction_.update(ic);
        });
    instance_->userInterfaceManager().registerAction("pinyin-prediction",
                                                     &predictionAction_);
    event_ = instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            auto *inputContext = keyEvent.inputContext();
            const auto *entry = instance_->inputMethodEntry(inputContext);
            if (!entry || entry->addon() != "pinyin") {
                return;
            }
            handle2nd3rdSelection(keyEvent);
        });

    pyConfig_.shuangpinProfile.annotation().setHidden(true);
    pyConfig_.showShuangpinMode.annotation().setHidden(true);
    pyConfig_.fuzzyConfig->partialSp.annotation().setHidden(true);

    deferredPreload_ = instance_->eventLoop().addDeferEvent([this](
                                                                EventSource *) {
        bool hasCloudPinyin = cloudpinyin() != nullptr;
        for (auto *configPtr : {&config_, &pyConfig_}) {
            configPtr->cloudPinyinEnabled.annotation().setHidden(
                !hasCloudPinyin);
            configPtr->cloudPinyinIndex.annotation().setHidden(!hasCloudPinyin);
            configPtr->cloudPinyinAnimation.annotation().setHidden(
                !hasCloudPinyin);
            configPtr->keepCloudPinyinPlaceHolder.annotation().setHidden(
                !hasCloudPinyin);
            configPtr->cloudpinyin.setHidden(!hasCloudPinyin);
        }
        deferredPreload_.reset();
        return true;
    });
}

PinyinEngine::~PinyinEngine() {}

void PinyinEngine::loadSymbols(const StandardPathFile &file) {
    if (file.fd() < 0) {
        return;
    }
    boost::iostreams::stream_buffer<boost::iostreams::file_descriptor_source>
        buffer(file.fd(),
               boost::iostreams::file_descriptor_flags::never_close_handle);
    std::istream in(&buffer);
    try {
        PINYIN_DEBUG() << "Loading symbol dict " << file.path();
        symbols_.load(in);
    } catch (const std::exception &e) {
        PINYIN_ERROR() << "Failed to load symbol dict " << file.path() << ": "
                       << e.what();
    }
}

void PinyinEngine::loadDict(const std::string &fullPath,
                            std::list<std::unique_ptr<TaskToken>> &taskTokens) {
    if (fullPath.empty()) {
        return;
    }
    ime_->dict()->addEmptyDict();
    PINYIN_DEBUG() << "Loading pinyin dict " << fullPath;
    std::packaged_task<libime::PinyinDictionary::TrieType()> task([fullPath]() {
        std::ifstream in(fullPath, std::ios::in | std::ios::binary);
        auto trie = libime::PinyinDictionary::load(
            in, libime::PinyinDictFormat::Binary);
        return trie;
    });
    taskTokens.push_back(worker_.addTask(
        std::move(task),
        [this, index = ime_->dict()->dictSize() - 1, fullPath](
            std::shared_future<libime::PinyinDictionary::TrieType> &future) {
            try {
                PINYIN_DEBUG()
                    << "Load pinyin dict " << fullPath << " finished.";
                ime_->dict()->setTrie(index, future.get());
            } catch (const std::exception &e) {
                PINYIN_ERROR() << "Failed to load pinyin dict " << fullPath
                               << ": " << e.what();
            }
        }));
}

void PinyinEngine::loadBuiltInDict() {
    const auto &standardPath = StandardPath::global();
    {
        auto file = standardPath.open(StandardPath::Type::PkgData,
                                      "pinyin/symbols", O_RDONLY);
        loadSymbols(file);
    }
    {
        auto file = standardPath.locate(StandardPath::Type::PkgData,
                                        "pinyin/chaizi.dict");
        loadDict(file, persistentTask_);
    }
    {
        auto file =
            standardPath.locate(StandardPath::Type::Data, "libime/extb.dict");
        // Try again with absolute libime path.
        if (file.empty()) {
            file = standardPath.locate(StandardPath::Type::Data,
                                       LIBIME_INSTALL_PKGDATADIR "/extb.dict");
        }
        loadDict(file, persistentTask_);
    }
    if (ime_->dict()->dictSize() !=
        libime::TrieDictionary::UserDict + 1 + NumBuiltInDict) {
        throw std::runtime_error("Failed to load built-in dictionary");
    }
}

void PinyinEngine::loadExtraDict() {
    const auto &standardPath = StandardPath::global();
    auto files =
        standardPath.locate(StandardPath::Type::PkgData, "pinyin/dictionaries",
                            filter::Suffix(".dict"));
    auto disableFiles =
        standardPath.locate(StandardPath::Type::PkgData, "pinyin/dictionaries",
                            filter::Suffix(".dict.disable"));
    FCITX_ASSERT(ime_->dict()->dictSize() >=
                 libime::TrieDictionary::UserDict + NumBuiltInDict + 1)
        << "Dict size: " << ime_->dict()->dictSize();
    tasks_.clear();
    ime_->dict()->removeFrom(libime::TrieDictionary::UserDict + NumBuiltInDict +
                             1);
    for (auto &file : files) {
        if (disableFiles.count(stringutils::concat(file.first, ".disable"))) {
            PINYIN_DEBUG() << "Dictionary: " << file.first << " is disabled.";
            continue;
        }
        PINYIN_DEBUG() << "Loading extra dictionary: " << file.first;
        loadDict(std::move(file.second), tasks_);
    }
}

void PinyinEngine::loadCustomPhrase() {
    const auto &standardPath = StandardPath::global();
    auto file = standardPath.open(StandardPath::Type::PkgData,
                                  "pinyin/customphrase", O_RDONLY);
    if (!file.isValid()) {
        customPhrase_.clear();
        return;
    }

    try {
        boost::iostreams::stream_buffer<
            boost::iostreams::file_descriptor_source>
            buffer(file.fd(),
                   boost::iostreams::file_descriptor_flags::never_close_handle);
        std::istream in(&buffer);
        customPhrase_.load(in, true);
    } catch (const std::exception &e) {
        PINYIN_ERROR() << e.what();
    }
}

void PinyinEngine::populateConfig() {
    if (*config_.firstRun) {
        config_.firstRun.setValue(false);
        safeSaveAsIni(config_, "conf/pinyin.conf");
        deferEvent_ = instance_->eventLoop().addDeferEvent([this](
                                                               EventSource *) {
            if (cloudpinyin() && !*config_.cloudPinyinEnabled &&
                notifications()) {
                const auto &key =
                    cloudpinyin()->call<ICloudPinyin::toggleKey>();

                std::string msg;
                if (key.empty()) {
                    msg = _("Do you want to enable cloudpinyin now for better "
                            "prediction? You can always toggle it later in "
                            "configuration.");
                } else {
                    msg = fmt::format(
                        _("Do you want to enable cloudpinyin now for better "
                          "prediction? You can always toggle it later in "
                          "configuration or by pressing {}."),
                        Key::keyListToString(key, KeyStringFormat::Localized));
                }
                std::vector<std::string> actions = {"yes", _("Yes"), "no",
                                                    _("No")};

                notifications()->call<INotifications::sendNotification>(
                    _("Pinyin"), 0, "fcitx-pinyin", _("Enable Cloudpinyin"),
                    msg, actions, -1,
                    [this](const std::string &action) {
                        if (action == "yes") {
                            config_.cloudPinyinEnabled.setValue(true);
                            save();
                        }
                    },
                    nullptr);
            }
            deferEvent_.reset();
            return true;
        });
    }
    ime_->setNBest(*config_.nbest);
    ime_->setPartialLongWordLimit(*config_.longWordLimit);
    ime_->setPreeditMode(*config_.showActualPinyinInPreedit
                             ? libime::PinyinPreeditMode::Pinyin
                             : libime::PinyinPreeditMode::RawText);
    if (*config_.shuangpinProfile == ShuangpinProfileEnum::Custom) {
        auto file = StandardPath::global().open(StandardPath::Type::PkgConfig,
                                                "pinyin/sp.dat", O_RDONLY);
        if (file.isValid()) {
            try {
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                          never_close_handle);
                std::istream in(&buffer);
                ime_->setShuangpinProfile(
                    std::make_shared<libime::ShuangpinProfile>(in));
            } catch (const std::exception &e) {
                PINYIN_ERROR() << e.what();
            }
        } else {
            PINYIN_ERROR() << "Failed to open shuangpin profile.";
        }
    } else {
        libime::ShuangpinBuiltinProfile profile;
#define TRANS_SP_PROFILE(PROFILE)                                              \
    case ShuangpinProfileEnum::PROFILE:                                        \
        profile = libime::ShuangpinBuiltinProfile::PROFILE;                    \
        break;
        switch (*config_.shuangpinProfile) {
            TRANS_SP_PROFILE(Ziranma)
            TRANS_SP_PROFILE(MS)
            TRANS_SP_PROFILE(Ziguang)
            TRANS_SP_PROFILE(ABC)
            TRANS_SP_PROFILE(Zhongwenzhixing)
            TRANS_SP_PROFILE(PinyinJiajia)
            TRANS_SP_PROFILE(Xiaohe)
        default:
            profile = libime::ShuangpinBuiltinProfile::Ziranma;
            break;
        }
        ime_->setShuangpinProfile(
            std::make_shared<libime::ShuangpinProfile>(profile));
    }

    // Always set a profile to avoid crash.
    if (!ime_->shuangpinProfile()) {
        ime_->setShuangpinProfile(std::make_shared<libime::ShuangpinProfile>(
            libime::ShuangpinBuiltinProfile::Ziranma));
    }

    libime::PinyinFuzzyFlags flags;
    const auto &fuzzyConfig = *config_.fuzzyConfig;
#define SET_FUZZY_FLAG(VAR, ENUM)                                              \
    if (*fuzzyConfig.VAR) {                                                    \
        flags |= libime::PinyinFuzzyFlag::ENUM;                                \
    }
    SET_FUZZY_FLAG(ue, VE_UE)
    SET_FUZZY_FLAG(commonTypo, CommonTypo)
    SET_FUZZY_FLAG(commonTypo, AdvancedTypo)
    SET_FUZZY_FLAG(inner, Inner)
    SET_FUZZY_FLAG(innerShort, InnerShort)
    SET_FUZZY_FLAG(partialFinal, PartialFinal)
    SET_FUZZY_FLAG(partialSp, PartialSp)
    SET_FUZZY_FLAG(v, V_U)
    SET_FUZZY_FLAG(an, AN_ANG)
    SET_FUZZY_FLAG(en, EN_ENG)
    SET_FUZZY_FLAG(ian, IAN_IANG)
    SET_FUZZY_FLAG(in, IN_ING)
    SET_FUZZY_FLAG(ou, U_OU)
    SET_FUZZY_FLAG(uan, UAN_UANG)
    SET_FUZZY_FLAG(c, C_CH)
    SET_FUZZY_FLAG(f, F_H)
    SET_FUZZY_FLAG(l, L_N)
    SET_FUZZY_FLAG(s, S_SH)
    SET_FUZZY_FLAG(z, Z_ZH)

    std::shared_ptr<libime::PinyinCorrectionProfile> correctionProfile;
    switch (*fuzzyConfig.correction) {
    case CorrectionLayout::None:
        break;
    case CorrectionLayout::Qwerty:
        correctionProfile = std::make_shared<libime::PinyinCorrectionProfile>(
            libime::BuiltinPinyinCorrectionProfile::Qwerty);
        break;
    }

    if (correctionProfile) {
        flags |= libime::PinyinFuzzyFlag::Correction;
        ime_->setCorrectionProfile(std::move(correctionProfile));
    }

    ime_->setFuzzyFlags(flags);

    quickphraseTriggerDict_.clear();
    for (std::string_view prefix : *config_.quickphraseTrigger) {
        if (prefix.empty()) {
            continue;
        }
        auto length = utf8::lengthValidated(prefix);
        if (length == utf8::INVALID_LENGTH || length < 1) {
            continue;
        }
        auto latinPartLength =
            utf8::ncharByteLength(prefix.begin(), length - 1);
        auto latinPart = prefix.substr(0, latinPartLength);

        if (std::any_of(latinPart.begin(), latinPart.end(), [](char c) {
                return !charutils::islower(c) && !charutils::isupper(c);
            })) {
            continue;
        }

        const uint32_t trigger =
            utf8::getChar(prefix.begin() + latinPartLength, prefix.end());
        if (trigger && trigger != utf8::INVALID_CHAR &&
            trigger != utf8::NOT_ENOUGH_SPACE) {
            quickphraseTriggerDict_[std::string(latinPart)].insert(trigger);
        }
    }
    PINYIN_DEBUG() << "Quick Phrase Trigger Dict: " << quickphraseTriggerDict_;
    ime_->dict()->setFlags(libime::TrieDictionary::UserDict + 1,
                           *config_.chaiziEnabled
                               ? libime::PinyinDictFlag::FullMatch
                               : libime::PinyinDictFlag::Disabled);
    ime_->dict()->setFlags(libime::TrieDictionary::UserDict + 2,
                           *config_.extBEnabled
                               ? libime::PinyinDictFlag::NoFlag
                               : libime::PinyinDictFlag::Disabled);

    pyConfig_ = config_;
}

void PinyinEngine::reloadConfig() {
    PINYIN_DEBUG() << "Reload pinyin config.";
    readAsIni(config_, "conf/pinyin.conf");
    populateConfig();
}
void PinyinEngine::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    // Request addons that gonna be used but not listed in optional
    // dependencies.
    fullwidth();
    chttrans();
    if (*config_.spellEnabled) {
        spell();
    }
    if (pinyinhelper()) {
        // Preload stroke data, since we gonna use it anyway.
        pinyinhelper()->call<IPinyinHelper::loadStroke>();
    }
    for (const auto *actionName : {"chttrans", "punctuation", "fullwidth"}) {
        if (auto *action =
                instance_->userInterfaceManager().lookupAction(actionName)) {
            inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                 action);
        }
    }
    inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                         &predictionAction_);
    auto *state = inputContext->propertyFor(&factory_);
    state->context_.setUseShuangpin(entry.uniqueName() == "shuangpin");
}

void PinyinEngine::deactivate(const fcitx::InputMethodEntry &entry,
                              fcitx::InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    do {
        if (event.type() != EventType::InputContextSwitchInputMethod) {
            break;
        }
        auto *state = inputContext->propertyFor(&factory_);
        if (state->mode_ == PinyinMode::Punctuation) {
            auto candidateList = inputContext->inputPanel().candidateList();
            if (!candidateList) {
                break;
            }
            auto index = candidateList->cursorIndex();
            if (index >= 0) {
                candidateList->candidate(index).select(inputContext);
            }
            break;
        }

        if (state->context_.empty()) {
            break;
        }

        switch (*config_.switchInputMethodBehavior) {
        case SwitchInputMethodBehavior::CommitPreedit:
            inputContext->commitString(preeditCommitString(inputContext));
            break;
        case SwitchInputMethodBehavior::CommitDefault: {
            inputContext->commitString(state->context_.sentence());
            break;
        }
        case SwitchInputMethodBehavior::Clear:
            break;
        }
    } while (0);
    reset(entry, event);
}

bool PinyinEngine::handlePuncCandidate(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    if (state->mode_ != PinyinMode::Punctuation) {
        return false;
    }
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList) {
        doReset(inputContext);
        return false;
    }
    if (event.key().check(FcitxKey_BackSpace)) {
        event.filterAndAccept();
        doReset(inputContext);
        return true;
    }
    if (!event.isVirtual()) {
        int idx = event.key().keyListIndex(selectionKeys_);
        if (idx == -1 && *config_.useKeypadAsSelectionKey) {
            idx = event.key().keyListIndex(numpadSelectionKeys_);
        }
        if (idx >= 0) {
            event.filterAndAccept();
            if (idx < candidateList->size()) {
                candidateList->candidate(idx).select(inputContext);
            }
            return true;
        }

        if (auto *movable = candidateList->toCursorMovable()) {
            if (event.key().checkKeyList(*config_.nextCandidate)) {
                movable->nextCandidate();
                updatePuncPreedit(inputContext);
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return true;
            }
            if (event.key().checkKeyList(*config_.prevCandidate)) {
                movable->prevCandidate();
                updatePuncPreedit(inputContext);
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                event.filterAndAccept();
                return true;
            }
        }
    }

    auto index = candidateList->cursorIndex();
    if (index >= 0) {
        candidateList->candidate(index).select(inputContext);
    }

    doReset(inputContext);
    return false;
}

bool PinyinEngine::handleCloudpinyinTrigger(KeyEvent &event) {
    if (cloudpinyin() && event.key().checkKeyList(
                             cloudpinyin()->call<ICloudPinyin::toggleKey>())) {
        config_.cloudPinyinEnabled.setValue(!*config_.cloudPinyinEnabled);
        safeSaveAsIni(config_, "conf/pinyin.conf");

        if (notifications()) {
            notifications()->call<INotifications::showTip>(
                "fcitx-cloudpinyin-toggle", _("Pinyin"), "",
                _("Cloud Pinyin Status"),
                *config_.cloudPinyinEnabled ? _("Cloud Pinyin is enabled.")
                                            : _("Cloud Pinyin is disabled."),
                1000);
        }
        if (*config_.cloudPinyinEnabled) {
            cloudpinyin()->call<ICloudPinyin::resetError>();
        }
        event.filterAndAccept();
        return true;
    }
    return false;
}

bool PinyinEngine::handle2nd3rdSelection(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList) {
        return false;
    }

    struct {
        const KeyList &list;
        int selection;
    } keyHandlers[] = {
        // Index starts with 0
        {*config_.secondCandidate, 1},
        {*config_.thirdCandidate, 2},
    };

    auto *state = inputContext->propertyFor(&factory_);
    int keyReleased = state->keyReleased_;
    int keyReleasedIndex = state->keyReleasedIndex_;
    // Keep these two values, and reset them in the state
    state->keyReleased_ = -1;
    state->keyReleasedIndex_ = -2;
    const bool isModifier = event.key().isModifier();
    if (event.isRelease()) {
        int idx = 0;
        for (auto &keyHandler : keyHandlers) {
            if (keyReleased == idx &&
                keyReleasedIndex == event.key().keyListIndex(keyHandler.list)) {
                if (isModifier) {
                    if (keyHandler.selection < candidateList->size()) {
                        candidateList->candidate(keyHandler.selection)
                            .select(inputContext);
                    }
                    event.filterAndAccept();
                    return true;
                }
                event.filter();
                return true;
            }
            idx++;
        }
    }

    if (!event.filtered() && !event.isRelease()) {
        int idx = 0;
        for (auto &keyHandler : keyHandlers) {
            auto keyIdx = event.key().keyListIndex(keyHandler.list);
            if (keyIdx >= 0) {
                state->keyReleased_ = idx;
                state->keyReleasedIndex_ = keyIdx;
                if (isModifier) {
                    // don't forward to input method, but make it pass
                    // through to client.
                    event.filter();
                    return true;
                }
                if (keyHandler.selection < candidateList->size()) {
                    candidateList->candidate(keyHandler.selection)
                        .select(inputContext);
                }
                event.filterAndAccept();
                return true;
            }
            idx++;
        }
    }
    return false;
}

bool PinyinEngine::handleCandidateList(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList) {
        return false;
    }
    auto *state = inputContext->propertyFor(&factory_);
    if (event.key().checkKeyList(*config_.currentCandidate) &&
        !state->predictWords_) {
        if (!candidateList->empty()) {
            event.filterAndAccept();
            int idx = candidateList->cursorIndex();
            if (idx < 0) {
                idx = 0;
            }
            candidateList->candidate(idx).select(inputContext);
            return true;
        }
    }

    if (event.isVirtual()) {
        return false;
    }

    int idx = event.key().keyListIndex(selectionKeys_);
    if (idx == -1 && *config_.useKeypadAsSelectionKey) {
        idx = event.key().keyListIndex(numpadSelectionKeys_);
    }
    if (idx >= 0) {
        event.filterAndAccept();
        if (idx < candidateList->size()) {
            candidateList->candidate(idx).select(inputContext);
        }
        return true;
    }

    if (event.key().checkKeyList(*config_.prevPage)) {
        auto *pageable = candidateList->toPageable();
        if (!pageable->hasPrev()) {
            if (pageable->usedNextBefore()) {
                event.filterAndAccept();
                return true;
            }
            // Only let key go through if it can reach handlePunc.
            auto c = Key::keySymToUnicode(event.key().sym());
            if (event.key().hasModifier() || !c) {
                event.filterAndAccept();
                return true;
            }
        } else {
            event.filterAndAccept();
            pageable->prev();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            return true;
        }
    }

    if (handleNextPage(event)) {
        return true;
    }

    if (auto *movable = candidateList->toCursorMovable()) {
        if (event.key().checkKeyList(*config_.nextCandidate)) {
            movable->nextCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
        if (event.key().checkKeyList(*config_.prevCandidate)) {
            movable->prevCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
    }
    return false;
}

bool PinyinEngine::handleNextPage(KeyEvent &event) const {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (event.key().checkKeyList(*config_.nextPage)) {
        event.filterAndAccept();
        candidateList->toPageable()->next();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        return true;
    }
    return false;
}

void PinyinEngine::updateStroke(InputContext *inputContext) {
    auto *state = inputContext->propertyFor(&factory_);
    auto &inputPanel = inputContext->inputPanel();
    inputPanel.reset();

    updatePreedit(inputContext);
    Text aux;
    aux.append(_("[Stroke Filtering]"));
    aux.append(pinyinhelper()->call<IPinyinHelper::prettyStrokeString>(
        state->strokeBuffer_.userInput()));
    inputPanel.setAuxUp(aux);

    auto candidateList = std::make_unique<CommonCandidateList>();
    candidateList->setPageSize(*config_.pageSize);
    candidateList->setCursorPositionAfterPaging(
        CursorPositionAfterPaging::ResetToFirst);

    auto *origCandidateList = state->strokeCandidateList_->toBulk();
    for (int i = 0; i < origCandidateList->totalSize(); i++) {
        const auto &candidate = origCandidateList->candidateFromAll(i);
        auto str = candidate.text().toStringForCommit();
        if (auto length = utf8::lengthValidated(str);
            length != utf8::INVALID_LENGTH && length >= 1) {
            auto charRange = utf8::MakeUTF8CharRange(str);
            bool strokeMatched = false;
            for (auto iter = std::begin(charRange), end = std::end(charRange);
                 iter != end; ++iter) {
                std::string chr(iter.charRange().first,
                                iter.charRange().second);
                auto stroke =
                    pinyinhelper()->call<IPinyinHelper::reverseLookupStroke>(
                        chr);
                if (stringutils::startsWith(stroke,
                                            state->strokeBuffer_.userInput())) {
                    strokeMatched = true;
                    break;
                }
            }
            if (strokeMatched) {
                candidateList->append<StrokeFilterCandidateWord>(
                    this, inputContext, candidate.text(), i);
            }
        }
    }
    candidateList->setSelectionKey(selectionKeys_);
    if (!candidateList->empty()) {
        candidateList->setGlobalCursorIndex(0);
    }
    candidateList->setActionableImpl(
        std::make_unique<PinyinActionableCandidateList>(this, inputContext));
    inputContext->inputPanel().setCandidateList(std::move(candidateList));
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::updateForgetCandidate(InputContext *inputContext) {
    auto *state = inputContext->propertyFor(&factory_);
    auto &inputPanel = inputContext->inputPanel();
    inputPanel.reset();

    updatePreedit(inputContext);
    Text aux(_("[Select the word to remove from history]"));
    inputPanel.setAuxUp(aux);

    auto candidateList = std::make_unique<CommonCandidateList>();
    candidateList->setPageSize(*config_.pageSize);
    candidateList->setCursorPositionAfterPaging(
        CursorPositionAfterPaging::ResetToFirst);

    auto *origCandidateList = state->forgetCandidateList_->toBulk();
    for (int i = 0; i < origCandidateList->totalSize(); i++) {
        const auto &candidate = origCandidateList->candidateFromAll(i);
        if (const auto *pyCandidate =
                dynamic_cast<const PinyinCandidateWord *>(&candidate)) {
            if (pyCandidate->idx_ >=
                    state->context_.candidatesToCursor().size() ||
                state->context_
                    .candidateFullPinyin(
                        state->context_.candidatesToCursor()[pyCandidate->idx_])
                    .empty()) {
                continue;
            }

            candidateList->append<ForgetCandidateWord>(
                this, pyCandidate->text(), pyCandidate->idx_);
        }
    }
    candidateList->setSelectionKey(selectionKeys_);
    if (!candidateList->empty()) {
        candidateList->setGlobalCursorIndex(0);
    }
    inputContext->inputPanel().setCandidateList(std::move(candidateList));
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::resetStroke(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&factory_);
    state->strokeCandidateList_.reset();
    state->strokeBuffer_.clear();
    if (state->mode_ == PinyinMode::StrokeFilter) {
        state->mode_ = PinyinMode::Normal;
    }
}

void PinyinEngine::resetForgetCandidate(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&factory_);
    state->forgetCandidateList_.reset();
    if (state->mode_ == PinyinMode::ForgetCandidate) {
        state->mode_ = PinyinMode::Normal;
    }
}

void PinyinEngine::forgetCandidate(InputContext *inputContext, size_t index) {
    auto *state = inputContext->propertyFor(&factory_);

    if (index < state->context_.candidatesToCursor().size()) {
        const auto &sentence = state->context_.candidatesToCursor()[index];
        // If this is a word, remove it from user dict.
        if (sentence.size() == 1) {
            auto py = state->context_.candidateFullPinyin(index);
            state->context_.ime()->dict()->removeWord(
                libime::PinyinDictionary::UserDict, py, sentence.toString());
        }
        for (const auto &word : sentence.sentence()) {
            state->context_.ime()->model()->history().forget(word->word());
        }
    }
    resetForgetCandidate(inputContext);
    doReset(inputContext);
}

void PinyinEngine::saveCustomPhrase() {
    instance_->eventDispatcher().scheduleWithContext(watch(), [this]() {
        StandardPath::global().safeSave(
            StandardPath::Type::PkgData, "pinyin/customphrase", [this](int fd) {
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_sink>
                    buffer(fd, boost::iostreams::file_descriptor_flags::
                                   never_close_handle);
                std::ostream out(&buffer);
                try {
                    customPhrase_.save(out);
                    return static_cast<bool>(out);
                } catch (const std::exception &e) {
                    PINYIN_ERROR()
                        << "Failed to save custom phrase: " << e.what();
                    return false;
                }
            });
    });
}

void PinyinEngine::pinCustomPhrase(InputContext *inputContext,
                                   const std::string &customPhrase) {
    auto *state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    // Precompute some values.
    const auto selectedLength = context.selectedLength();
    const auto pyLength = context.cursor() > selectedLength
                              ? context.cursor() - selectedLength
                              : std::string::npos;
    const auto py = context.userInput().substr(selectedLength, pyLength);
    customPhrase_.pinPhrase(py, customPhrase);

    resetStroke(inputContext);
    updateUI(inputContext);
    saveCustomPhrase();
}

void PinyinEngine::deleteCustomPhrase(InputContext *inputContext,
                                      const std::string &customPhrase) {
    auto *state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    // Precompute some values.
    const auto selectedLength = context.selectedLength();
    const auto pyLength = context.cursor() > selectedLength
                              ? context.cursor() - selectedLength
                              : std::string::npos;
    const auto py = context.userInput().substr(selectedLength, pyLength);
    customPhrase_.removePhrase(py, customPhrase);

    resetStroke(inputContext);
    updateUI(inputContext);
    saveCustomPhrase();
}

bool PinyinEngine::handleStrokeFilter(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    auto *state = inputContext->propertyFor(&factory_);
    if (state->mode_ == PinyinMode::Normal) {
        if (candidateList && !candidateList->empty() &&
            candidateList->toBulk() &&
            event.key().checkKeyList(*config_.selectByStroke) &&
            pinyinhelper()) {
            resetStroke(inputContext);
            state->strokeCandidateList_ = std::move(candidateList);
            state->mode_ = PinyinMode::StrokeFilter;
            updateStroke(inputContext);
            handleNextPage(event);

            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (state->mode_ != PinyinMode::StrokeFilter) {
        return false;
    }

    event.filterAndAccept();
    // A special case that allow prev page to quit stroke filtering.
    if ((state->strokeBuffer_.empty() &&
         event.key().checkKeyList(*config_.prevPage))) {
        auto candidateList = inputContext->inputPanel().candidateList();
        if (candidateList && candidateList->toPageable() &&
            candidateList->toPageable()->currentPage() <= 1) {
            resetStroke(inputContext);
            updateUI(inputContext);
            return true;
        }
    }

    if (handleCandidateList(event)) {
        return true;
    }
    // Skip all key combination.
    if (event.key().states().testAny(KeyState::SimpleMask)) {
        return true;
    }

    if (event.key().check(FcitxKey_Escape)) {
        resetStroke(inputContext);
        updateUI(inputContext);
        return true;
    }
    if (event.key().check(FcitxKey_BackSpace)) {
        // Do backspace is stroke is not empty.
        if (!state->strokeBuffer_.empty()) {
            state->strokeBuffer_.backspace();
            updateStroke(inputContext);
        } else {
            // Exit stroke mode when stroke buffer is empty.
            resetStroke(inputContext);
            updateUI(inputContext);
        }
        return true;
    }
    // if it gonna commit something
    auto c = Key::keySymToUnicode(event.key().sym());
    if (!c) {
        return true;
    }

    if (event.key().check(FcitxKey_h) || event.key().check(FcitxKey_p) ||
        event.key().check(FcitxKey_s) || event.key().check(FcitxKey_n) ||
        event.key().check(FcitxKey_z)) {
        static const std::unordered_map<FcitxKeySym, char> strokeMap{
            {FcitxKey_h, '1'},
            {FcitxKey_s, '2'},
            {FcitxKey_p, '3'},
            {FcitxKey_n, '4'},
            {FcitxKey_z, '5'}};
        if (auto iter = strokeMap.find(event.key().sym());
            iter != strokeMap.end()) {
            state->strokeBuffer_.type(iter->second);
            updateStroke(inputContext);
        }
    }

    return true;
}

bool PinyinEngine::handleForgetCandidate(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    auto *state = inputContext->propertyFor(&factory_);
    if (state->mode_ == PinyinMode::Normal) {
        if (!state->predictWords_ && candidateList && !candidateList->empty() &&
            candidateList->toBulk() &&
            event.key().checkKeyList(*config_.forgetWord)) {
            resetForgetCandidate(inputContext);
            state->forgetCandidateList_ = std::move(candidateList);
            state->mode_ = PinyinMode::ForgetCandidate;
            updateForgetCandidate(inputContext);
            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (state->mode_ != PinyinMode::ForgetCandidate) {
        return false;
    }

    event.filterAndAccept();
    // Skip all key combination.
    if (event.key().states().testAny(KeyState::SimpleMask)) {
        return true;
    }

    if (event.key().check(FcitxKey_Escape)) {
        resetForgetCandidate(inputContext);
        updateUI(inputContext);
        return true;
    }
    return true;
}

bool PinyinEngine::handlePunc(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    auto *state = inputContext->propertyFor(&factory_);
    if (event.filtered()) {
        return false;
    }
    // if it gonna commit something
    auto c = Key::keySymToUnicode(event.key().sym());
    if (event.key().hasModifier() || !c) {
        // Handle quick phrase with modifier
        if (event.key().check(*config_.quickphraseKey) && quickphrase()) {
            quickphrase()->call<IQuickPhrase::trigger>(inputContext, "", "", "",
                                                       "", Key());
            event.filterAndAccept();
            return true;
        }
        return false;
    }
    if (candidateList && !candidateList->empty()) {
        candidateList->candidate(0).select(inputContext);
    }

    std::string punc;
    std::string puncAfter;
    // skip key pad
    if (c && !event.key().isKeyPad()) {
        auto candidates =
            punctuation()->call<IPunctuation::getPunctuationCandidates>("zh_CN",
                                                                        c);
        auto pushResult = punctuation()->call<IPunctuation::pushPunctuationV2>(
            "zh_CN", inputContext, c);
        if (candidates.size() == 1) {
            std::tie(punc, puncAfter) = pushResult;
        } else if (candidates.size() > 1) {
            updatePuncCandidate(inputContext, utf8::UCS4ToUTF8(c), candidates);
            event.filterAndAccept();
            return true;
        }
    }
    if (!event.isVirtual() && event.key().check(*config_.quickphraseKey) &&
        quickphrase()) {
        auto keyString = utf8::UCS4ToUTF8(c);
        // s is punc or key
        auto output = !punc.empty() ? (punc + puncAfter) : keyString;
        // alt is key or empty
        auto altOutput = !punc.empty() ? keyString : "";
        // if no punc: key -> key (s = key, alt = empty)
        // if there's punc: key -> punc, return -> key (s = punc, alt =
        // key)
        std::string text;
        if (!output.empty()) {
            if (!altOutput.empty()) {
                text = fmt::format(_("Press {} for {} and Return for {}"),
                                   keyString, output, altOutput);
            } else {
                text = fmt::format(_("Press {} for {}"), keyString, altOutput);
            }
        }
        quickphrase()->call<IQuickPhrase::trigger>(
            inputContext, text, "", output, altOutput, *config_.quickphraseKey);
        event.filterAndAccept();
        return true;
    }
    if (!punc.empty()) {
        event.filterAndAccept();
        auto paired = punc + puncAfter;
        if (inputContext->capabilityFlags().test(
                CapabilityFlag::CommitStringWithCursor)) {
            if (size_t length = utf8::lengthValidated(punc);
                length != 0 && length != utf8::INVALID_LENGTH) {
                inputContext->commitStringWithCursor(paired, length);
            } else {
                inputContext->commitString(paired);
            }
        } else {
            inputContext->commitString(paired);
            if (size_t length = utf8::lengthValidated(puncAfter);
                length != 0 && length != utf8::INVALID_LENGTH) {
                for (size_t i = 0; i < length; i++) {
                    inputContext->forwardKey(Key(FcitxKey_Left));
                }
            }
        }
    }
    state->lastIsPunc_ = true;
    return false;
}

bool PinyinEngine::handleCompose(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    if (event.key().states().testAny(
            KeyStates{KeyState::Ctrl, KeyState::Super}) ||
        state->mode_ != PinyinMode::Normal) {
        return false;
    }
    auto candidateList = inputContext->inputPanel().candidateList();
    if (event.filtered()) {
        return false;
    }
    auto compose =
        instance_->processComposeString(inputContext, event.key().sym());
    if (!compose) {
        // invalid compose or in the middle of compose.
        event.filterAndAccept();
        return true;
    }
    // Handle the key just like punc select.
    if (!compose->empty()) {
        // Reset predict in case we are in predict.
        resetPredict(inputContext);
        // punc like auto selection.
        auto candidateList = inputContext->inputPanel().candidateList();
        if (candidateList && !candidateList->empty()) {
            candidateList->candidate(0).select(inputContext);
        }
        inputContext->commitString(*compose);
        event.filterAndAccept();
        return true;
    }
    return false;
}

void PinyinEngine::resetPredict(InputContext *inputContext) {
    auto *state = inputContext->propertyFor(&factory_);
    if (!state->predictWords_) {
        return;
    }
    state->predictWords_.reset();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    PINYIN_DEBUG() << "Pinyin receive key: " << event.key() << " "
                   << event.isRelease();
    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);

    // 2nd/3rd selection is allowed to be modifier only, handle them before we
    // skip the release.
    if (handle2nd3rdSelection(event)) {
        return;
    }

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    if (handlePuncCandidate(event)) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    if (handleCloudpinyinTrigger(event)) {
        return;
    }

    auto candidateList = inputContext->inputPanel().candidateList();
    bool lastIsPunc = state->lastIsPunc_;
    state->lastIsPunc_ = false;

    if (handleStrokeFilter(event)) {
        return;
    }

    if (handleCompose(event)) {
        return;
    }

    // handle number key selection and prev/next page/candidate.
    if (handleCandidateList(event)) {
        return;
    }

    if (handleForgetCandidate(event)) {
        return;
    }

    // In prediction, as long as it's not candidate selection, clear, then
    // fallback
    // to remaining operation.
    if (state->predictWords_) {
        resetPredict(inputContext);
        if (event.key().check(FcitxKey_Escape) ||
            (isAndroid() && event.key().check(FcitxKey_BackSpace)) ||
            event.key().check(FcitxKey_Delete)) {
            event.filterAndAccept();
            return;
        }
    }

    auto checkSp = [this](const KeyEvent &event, PinyinState *state) {
        auto shuangpinProfile = ime_->shuangpinProfile();
        if (!state->context_.useShuangpin() || !shuangpinProfile ||
            !event.key().isSimple()) {
            return false;
        }
        // event.key().isSimple() make sure the return value is within range of
        // char.
        char chr = Key::keySymToUnicode(event.key().sym());
        return (!state->context_.empty() &&
                shuangpinProfile->validInput().count(chr)) ||
               (state->context_.empty() &&
                shuangpinProfile->validInitial().count(chr));
    };

    if (!event.key().hasModifier() && quickphrase()) {
        const auto iter =
            quickphraseTriggerDict_.find(state->context_.userInput());
        if (iter != quickphraseTriggerDict_.end() && !iter->second.empty() &&
            iter->second.count(Key::keySymToUnicode(event.key().sym()))) {
            std::string text = state->context_.userInput();
            text.append(Key::keySymToUTF8(event.key().sym()));
            doReset(inputContext);
            quickphrase()->call<IQuickPhrase::trigger>(inputContext, "", "", "",
                                                       "", Key());
            quickphrase()->call<IQuickPhrase::setBuffer>(inputContext, text);

            return event.filterAndAccept();
        }
    }

    if (event.key().isLAZ() || event.key().isUAZ() ||
        (event.key().check(FcitxKey_apostrophe) && !state->context_.empty()) ||
        checkSp(event, state)) {
        // first v, use it to trigger quickphrase
        if (!state->context_.useShuangpin() && quickphrase() &&
            *config_.useVAsQuickphrase && event.key().check(FcitxKey_v) &&
            state->context_.empty()) {

            quickphrase()->call<IQuickPhrase::trigger>(
                inputContext, "", "v", "", "", Key(FcitxKey_None));
            event.filterAndAccept();
            return;
        }
        event.filterAndAccept();
        if (!state->context_.type(Key::keySymToUTF8(event.key().sym()))) {
            return;
        }
    } else if (!state->context_.empty()) {
        // key to handle when it is not empty.
        if (event.key().check(FcitxKey_BackSpace)) {
            if (*config_.useBackSpaceToUnselect &&
                state->context_.selectedLength()) {
                state->context_.cancel();
            } else {
                state->context_.backspace();
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Delete) ||
                   event.key().check(FcitxKey_KP_Delete)) {
            state->context_.del();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_BackSpace, KeyState::Ctrl)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.pinyinBeforeCursor();
            if (cursor >= 0) {
                state->context_.erase(cursor, state->context_.cursor());
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Delete, KeyState::Ctrl) ||
                   event.key().check(FcitxKey_KP_Delete, KeyState::Ctrl)) {
            auto cursor = state->context_.pinyinAfterCursor();
            if (cursor >= 0 &&
                static_cast<size_t>(cursor) <= state->context_.size()) {
                state->context_.erase(state->context_.cursor(), cursor);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Home) ||
                   event.key().check(FcitxKey_KP_Home)) {
            state->context_.setCursor(state->context_.selectedLength());
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_End) ||
                   event.key().check(FcitxKey_KP_End)) {
            state->context_.setCursor(state->context_.size());
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Left) ||
                   event.key().check(FcitxKey_KP_Left)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.cursor();
            if (cursor > 0) {
                state->context_.setCursor(cursor - 1);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right) ||
                   event.key().check(FcitxKey_KP_Right)) {
            auto cursor = state->context_.cursor();
            if (cursor < state->context_.size()) {
                state->context_.setCursor(cursor + 1);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Left, KeyState::Ctrl) ||
                   event.key().check(FcitxKey_KP_Left, KeyState::Ctrl)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.pinyinBeforeCursor();
            if (cursor >= 0) {
                state->context_.setCursor(cursor);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right, KeyState::Ctrl) ||
                   event.key().check(FcitxKey_KP_Right, KeyState::Ctrl)) {
            auto cursor = state->context_.pinyinAfterCursor();
            if (cursor >= 0 &&
                static_cast<size_t>(cursor) <= state->context_.size()) {
                state->context_.setCursor(cursor);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Escape)) {
            state->context_.clear();
            event.filterAndAccept();
        } else if (event.key().checkKeyList(*config_.commitRawInput)) {
            inputContext->commitString(preeditCommitString(inputContext));
            state->context_.clear();
            event.filterAndAccept();
        } else if (int idx =
                       event.key().keyListIndex(*config_.selectCharFromPhrase);
                   idx >= 0) {
            if (candidateList && !candidateList->empty() &&
                candidateList->cursorIndex() >= 0) {
                const auto &candidate =
                    candidateList->candidate(candidateList->cursorIndex());
                auto str = candidate.text().toStringForCommit();
                // Validate string and length.
                if (auto len = utf8::lengthValidated(str);
                    len != utf8::INVALID_LENGTH &&
                    len > static_cast<size_t>(idx)) {
                    // Get idx-th char.
                    const std::string_view chr =
                        std::next(utf8::MakeUTF8CharRange(str).begin(), idx)
                            .view();
                    auto segmentLength = state->context_.size() -
                                         state->context_.selectedLength();
                    const auto *pyCandidate =
                        dynamic_cast<const PinyinCandidateWord *>(&candidate);
                    if (pyCandidate) {
                        const auto &contextCandidates =
                            state->context_.candidatesToCursor();
                        if (pyCandidate->idx_ < contextCandidates.size()) {
                            const auto &sentence =
                                contextCandidates[pyCandidate->idx_].sentence();
                            const auto candidateSegmentLength =
                                sentence.back()->to()->index();
                            segmentLength =
                                std::min(segmentLength, candidateSegmentLength);
                        }
                    }
                    state->context_.selectCustom(segmentLength, chr);
                    event.filterAndAccept();
                }
            }
        }
    } else if (event.key().check(FcitxKey_BackSpace)) {
        if (lastIsPunc) {
            const std::string &puncStr =
                punctuation()->call<IPunctuation::cancelLast>("zh_CN",
                                                              inputContext);
            if (!puncStr.empty()) {
                // forward the original key is the best choice.
                state->cancelLastEvent_ = instance()->eventLoop().addTimeEvent(
                    CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 300, 0,
                    [this, ref = inputContext->watch(),
                     puncStr = puncStr](EventSourceTime *, uint64_t) {
                        if (auto *inputContext = ref.get()) {
                            inputContext->commitString(puncStr);
                            auto *state = inputContext->propertyFor(&factory_);
                            state->cancelLastEvent_.reset();
                        }
                        return true;
                    });
                event.filter();
                return;
            }
        }
    }
    if (handlePunc(event)) {
        return;
    }

    if (event.filtered() && event.accepted()) {
        updateUI(inputContext);
    }
}

void PinyinEngine::setSubConfig(const std::string &path,
                                const RawConfig &config) {
    FCITX_UNUSED(config);
    if (path == "dictmanager") {
        loadExtraDict();
    } else if (path == "clearuserdict") {
        ime_->dict()->clear(libime::PinyinDictionary::UserDict);
    } else if (path == "clearalldict") {
        ime_->dict()->clear(libime::PinyinDictionary::UserDict);
        ime_->model()->history().clear();
    } else if (path == "customphrase") {
        loadCustomPhrase();
    }
}

void PinyinEngine::reset(const InputMethodEntry & /*entry*/,
                         InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    doReset(inputContext);
}

void PinyinEngine::doReset(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&factory_);
    resetStroke(inputContext);
    resetForgetCandidate(inputContext);
    state->mode_ = PinyinMode::Normal;
    state->context_.clear();
    state->predictWords_.reset();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    // state->lastIsPunc_ = false;

    state->keyReleased_ = -1;
    state->keyReleasedIndex_ = -2;
    instance_->resetCompose(inputContext);
}

void PinyinEngine::save() {
    safeSaveAsIni(config_, "conf/pinyin.conf");
    const auto &standardPath = StandardPath::global();
    standardPath.safeSave(
        StandardPath::Type::PkgData, "pinyin/user.dict", [this](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            try {
                ime_->dict()->save(libime::PinyinDictionary::UserDict, out,
                                   libime::PinyinDictFormat::Binary);
                return static_cast<bool>(out);
            } catch (const std::exception &e) {
                PINYIN_ERROR() << "Failed to save pinyin dict: " << e.what();
                return false;
            }
        });
    standardPath.safeSave(
        StandardPath::Type::PkgData, "pinyin/user.history", [this](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            try {
                ime_->model()->save(out);
                return true;
            } catch (const std::exception &e) {
                PINYIN_ERROR() << "Failed to save pinyin history: " << e.what();
                return false;
            }
        });
}

std::string PinyinEngine::subMode(const InputMethodEntry &entry,
                                  InputContext &inputContext) {
    FCITX_UNUSED(inputContext);
    if (entry.uniqueName() == "shuangpin" && *config_.showShuangpinMode &&
        *config_.shuangpinProfile != ShuangpinProfileEnum::Custom) {
        return ShuangpinProfileEnumI18NAnnotation::toString(
            *config_.shuangpinProfile);
    }
    return {};
}

const Configuration *PinyinEngine::getConfig() const { return &config_; }

const Configuration *
PinyinEngine::getConfigForInputMethod(const InputMethodEntry &entry) const {
    // Hide shuangpin options.
    if (entry.uniqueName() == "pinyin") {
        return &pyConfig_;
    }
    return &config_;
}

void PinyinEngine::invokeActionImpl(const InputMethodEntry &entry,
                                    InvokeActionEvent &event) {
    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    auto &inputPanel = inputContext->inputPanel();
    if (event.cursor() < 0 ||
        event.action() != InvokeActionEvent::Action::LeftClick ||
        !inputContext->capabilityFlags().test(CapabilityFlag::Preedit)) {
        return InputMethodEngineV3::invokeActionImpl(entry, event);
    }
    auto [clientPreedit, _] = this->preedit(inputContext);

    auto preedit = clientPreedit.toString();
    size_t cursor = event.cursor();
    if (inputPanel.clientPreedit().toString() != clientPreedit.toString() ||
        inputPanel.clientPreedit().cursor() != clientPreedit.cursor() ||
        cursor > utf8::length(preedit)) {
        return InputMethodEngineV3::invokeActionImpl(entry, event);
    }

    event.filter();

    auto preeditWithCursor = context.preeditWithCursor();
    auto selectedSentence = context.selectedSentence();
    // The logic here need to match ::preedit()
    switch (*config_.preeditMode) {
    case PreeditMode::No:
        break;
    case PreeditMode::ComposingPinyin:
        if (utf8::length(selectedSentence) > cursor) {
            // If cursor is with in selected sentence range, cancel until cursor
            // is covered.
            do {
                context.cancel();
            } while (utf8::length(context.selectedSentence()) > cursor);
            context.setCursor(context.selectedLength());
        } else {
            // If cursor is with in the pinyin range, move to the left most and
            // then move it around. This is a easy way to cover Shuangpin and
            // Preedit Mode difference. setCursor should be cheap operation if
            // it does not cancel any selection.
            context.setCursor(context.selectedLength());
            while (context.cursor() < context.size()) {
                auto [preeditText, preeditCursor] = context.preeditWithCursor();
                if (utf8::length(preeditText.begin(),
                                 preeditText.begin() + preeditCursor) <
                    cursor) {
                    state->context_.setCursor(context.cursor() + 1);
                } else {
                    break;
                }
            }
            auto [preeditText, preeditCursor] = context.preeditWithCursor();
            if (utf8::length(preeditText.begin(),
                             preeditText.begin() + preeditCursor) > cursor) {
                state->context_.setCursor(context.cursor() - 1);
            }
        }
        break;
    case PreeditMode::CommitPreview:
        if (utf8::length(selectedSentence) > cursor) {
            do {
                context.cancel();
            } while (utf8::length(context.selectedSentence()) > cursor);
        }
        break;
    }
    updateUI(inputContext);
}

void PinyinEngine::cloudPinyinSelected(InputContext *inputContext,
                                       const std::string &selected,
                                       const std::string &word) {
    auto *state = inputContext->propertyFor(&factory_);
    auto words = state->context_.selectedWords();
    // This ensure us to convert pinyin to the right one.
    auto preedit = state->context_.preedit(libime::PinyinPreeditMode::RawText);
    // preedit is "selected sentence" + Pinyin.
    do {
        // Validate selected is still the same.
        if (!stringutils::startsWith(preedit, selected)) {
            words.clear();
            break;
        }
        // Get segmented pinyin.
        preedit = preedit.substr(selected.size());
        auto pinyins = stringutils::split(preedit, " '");
        std::string_view wordView = word;
        // Check word length matches pinyin length.
        if (pinyins.empty() || pinyins.size() != utf8::length(word)) {
            break;
        }
        const auto &candidates = state->context_.candidates();
        auto pinyinsIter = pinyins.begin();
        auto pinyinsEnd = pinyins.end();
        if (!candidates.empty()) {
            const auto &bestSentence = candidates[0].sentence();
            auto iter = bestSentence.begin();
            auto end = bestSentence.end();
            while (iter != end) {
                auto consumed = wordView;
                if (!consumePrefix(consumed, (*iter)->word())) {
                    break;
                }
                if (!(*iter)->word().empty()) {
                    words.push_back((*iter)->word());
                    PINYIN_DEBUG()
                        << "Cloud Pinyin can reuse segment " << (*iter)->word();
                    const auto *pinyinNode =
                        static_cast<const libime::PinyinLatticeNode *>(*iter);
                    auto pinyinSize = pinyinNode->encodedPinyin().size() / 2;
                    if (pinyinSize &&
                        static_cast<size_t>(std::distance(
                            pinyinsIter, pinyinsEnd)) >= pinyinSize) {
                        pinyinsIter += pinyinSize;
                    } else {
                        break;
                    }
                }
                wordView = consumed;
                iter++;
            }
        }
        // if pinyin is not valid, it may throw
        try {
            if (utf8::length(wordView) == 1 &&
                std::all_of(words.begin(), words.end(),
                            [](const std::string &w) {
                                return utf8::length(w) == 1;
                            })) {
                words = state->context_.selectedWords();
                auto joined =
                    stringutils::join(pinyins.begin(), pinyins.end(), "'");
                words.push_back(word);
                ime_->dict()->addWord(libime::PinyinDictionary::UserDict,
                                      joined, word);
            } else {
                if (state->context_.useShuangpin()) {
                    bool end = false;
                    for (auto &sppinyin :
                         MakeIterRange(pinyinsIter, pinyinsEnd)) {
                        // Likely something goes wrong.
                        if (sppinyin.size() > 2) {
                            end = true;
                            break;
                        }
                        sppinyin = libime::PinyinEncoder::shuangpinToPinyin(
                            sppinyin, *ime_->shuangpinProfile());
                        if (sppinyin.empty()) {
                            end = true;
                            break;
                        }
                    }
                    if (end) {
                        break;
                    }
                }
                auto joined = stringutils::join(pinyinsIter, pinyinsEnd, "'");
                PINYIN_DEBUG()
                    << "Cloud pinyin saves word: " << wordView << " " << joined;
                ime_->dict()->addWord(libime::PinyinDictionary::UserDict,
                                      joined, wordView);
                words.push_back(std::string{wordView});
            }
            ime_->model()->history().add(words);
        } catch (const std::exception &e) {
            PINYIN_DEBUG() << "Failed to save cloudpinyin: " << e.what();
        }
    } while (0);
    state->context_.clear();
    inputContext->commitString(selected + word);
    inputContext->inputPanel().reset();
    if (*config_.predictionEnabled) {
        state->predictWords_ = std::move(words);
        updatePredict(inputContext);
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PinyinEngineFactory)
