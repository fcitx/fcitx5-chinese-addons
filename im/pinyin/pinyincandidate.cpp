/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "pinyincandidate.h"
#include "../../modules/cloudpinyin/cloudpinyin_public.h"
#include "pinyin.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcitx-utils/event.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx/candidateaction.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <libime/core/historybigram.h>
#include <libime/core/lattice.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyindictionary.h>
#include <string>
#include <utility>
#include <vector>

namespace fcitx {

PinyinPredictCandidateWord::PinyinPredictCandidateWord(PinyinEngine *engine,
                                                       std::string word)
    : CandidateWord(Text(word)), engine_(engine), word_(std::move(word)) {}

void PinyinPredictCandidateWord::select(InputContext *inputContext) const {
    inputContext->commitString(word_);
    auto *state = inputContext->propertyFor(&engine_->factory());
    if (!state->predictWords_) {
        state->predictWords_.emplace();
    }
    auto &predictWords = *state->predictWords_;
    predictWords.push_back(word_);
    // Max history size.
    constexpr size_t maxHistorySize = 5;
    if (predictWords.size() > maxHistorySize) {
        predictWords.erase(predictWords.begin(), predictWords.begin() +
                                                     predictWords.size() -
                                                     maxHistorySize);
    }
    engine_->updatePredict(inputContext);
}

PinyinPredictDictCandidateWord::PinyinPredictDictCandidateWord(
    PinyinEngine *engine, std::string word)
    : CandidateWord(Text(word)), engine_(engine), word_(std::move(word)) {}

void PinyinPredictDictCandidateWord::select(InputContext *inputContext) const {
    inputContext->commitString(word_);
    auto *state = inputContext->propertyFor(&engine_->factory());
    if (!state->predictWords_) {
        state->predictWords_.emplace();
    }
    // Append to last word, instead of push back.
    if (!state->predictWords_->empty()) {
        state->predictWords_->back().append(word_);
    }
    engine_->updatePredict(inputContext);
}

ForgettableCandidateInterface::~ForgettableCandidateInterface() = default;

InsertableAsCustomPhraseInterface::~InsertableAsCustomPhraseInterface() =
    default;

PinyinAbstractExtraCandidateWordInterface::
    PinyinAbstractExtraCandidateWordInterface(CandidateWord &cand, int order)
    : cand_(cand), order_(order) {}

PinyinAbstractExtraCandidateWordInterface::
    ~PinyinAbstractExtraCandidateWordInterface() = default;

StrokeCandidateWord::StrokeCandidateWord(PinyinEngine *engine, std::string hz,
                                         const std::string &py, int order)
    : PinyinAbstractExtraCandidateWordInterface(*this, order), engine_(engine),
      hz_(std::move(hz)) {
    setText(Text(hz_));
    if (!py.empty()) {
        setComment(Text(py));
    }
}

void StrokeCandidateWord::select(InputContext *inputContext) const {
    inputContext->commitString(hz_);
    engine_->doReset(inputContext);
}

CustomPhraseCandidateWord::CustomPhraseCandidateWord(
    PinyinEngine *engine, int order, size_t inputLength, std::string value,
    std::string customPhraseString)
    : PinyinAbstractExtraCandidateWordInterface(*this, order), engine_(engine),
      inputLength_(inputLength),
      customPhraseString_(std::move(customPhraseString)) {
    setText(Text(std::move(value)));
}

void CustomPhraseCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto &context = state->context_;
    context.selectCustom(inputLength_, text().toString());
    engine_->updateUI(inputContext);
}

PinyinPunctuationCandidateWord::PinyinPunctuationCandidateWord(
    const PinyinEngine *engine, std::string word, bool isHalf)
    : engine_(engine), word_(std::move(word)) {
    setText(Text(word_));
    if (isHalf) {
        setComment(Text(_("(Half)")));
    }
}

void PinyinPunctuationCandidateWord::select(InputContext *inputContext) const {
    inputContext->commitString(word_);
    engine_->doReset(inputContext);
}

StrokeFilterCandidateWord::StrokeFilterCandidateWord(PinyinEngine *engine,
                                                     InputContext *inputContext,
                                                     Text text, int index)
    : engine_(engine), inputContext_(inputContext), index_(index) {
    setText(std::move(text));
}

std::string StrokeFilterCandidateWord::customPhraseString() const {
    auto *state = inputContext_->propertyFor(&engine_->factory());
    if (!state->strokeCandidateList_ ||
        state->strokeCandidateList_->toBulk()->totalSize() <= index_) {
        return "";
    }
    // Forward the selection to internal candidate list.
    const auto &candidate =
        state->strokeCandidateList_->toBulk()->candidateFromAll(index_);
    return static_cast<const PinyinCandidateWord &>(candidate)
        .customPhraseString();
}

size_t StrokeFilterCandidateWord::candidateIndex() const {
    auto *state = inputContext_->propertyFor(&engine_->factory());
    if (!state->strokeCandidateList_ ||
        state->strokeCandidateList_->toBulk()->totalSize() <= index_) {
        return 0;
    }
    // Forward the selection to internal candidate list.
    const auto &candidate =
        state->strokeCandidateList_->toBulk()->candidateFromAll(index_);
    return static_cast<const PinyinCandidateWord &>(candidate).candidateIndex();
}

void StrokeFilterCandidateWord::select(InputContext *inputContext) const {
    if (inputContext != inputContext_) {
        return;
    }
    auto *state = inputContext->propertyFor(&engine_->factory());
    if (!state->strokeCandidateList_ ||
        state->strokeCandidateList_->toBulk()->totalSize() <= index_) {
        FCITX_ERROR() << "Stroke candidate is not consistent. Probably a "
                         "bug in implementation";
        return;
    }
    // Forward the selection to internal candidate list.
    state->strokeCandidateList_->toBulk()->candidateFromAll(index_).select(
        inputContext);
    engine_->resetStroke(inputContext);
}

ForgetCandidateWord::ForgetCandidateWord(PinyinEngine *engine, Text text,
                                         size_t index)
    : engine_(engine), index_(index) {
    setText(std::move(text));
}

void ForgetCandidateWord::select(InputContext *inputContext) const {
    engine_->forgetCandidate(inputContext, index_);
}

LuaCandidateWord::LuaCandidateWord(PinyinEngine *engine, std::string word)
    : engine_(engine), word_(std::move(word)) {
    setText(Text(word_));
}

void LuaCandidateWord::select(InputContext *inputContext) const {
    inputContext->commitString(word_);
    engine_->doReset(inputContext);
}

SymbolCandidateWord::SymbolCandidateWord(PinyinEngine *engine,
                                         std::string symbol,
                                         const libime::SentenceResult &result,
                                         bool isFull)
    : engine_(engine), symbol_(std::move(symbol)),
      candidateSegmentLength_(result.sentence().back()->to()->index()),
      isFull_(isFull) {
    setText(Text(symbol_));
    bool validPinyin = std::all_of(
        result.sentence().begin(), result.sentence().end(),
        [](const libime::LatticeNode *node) {
            if (node->word().empty()) {
                return true;
            }
            const auto *pinyinNode =
                static_cast<const libime::PinyinLatticeNode *>(node);
            return !pinyinNode->encodedPinyin().empty() &&
                   pinyinNode->encodedPinyin().size() % 2 == 0;
        });
    if (validPinyin) {
        for (const auto *node : result.sentence()) {
            const auto *pinyinNode =
                static_cast<const libime::PinyinLatticeNode *>(node);
            encodedPinyin_.insert(encodedPinyin_.end(),
                                  pinyinNode->encodedPinyin().begin(),
                                  pinyinNode->encodedPinyin().end());
        }
    }
}

std::string SymbolCandidateWord::customPhraseString() const {
    if (isFull_) {
        return symbol_;
    }
    return "";
}

void SymbolCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto segmentLength =
        state->context_.size() - state->context_.selectedLength();
    segmentLength = std::min(segmentLength, candidateSegmentLength_);
    state->context_.selectCustom(segmentLength, symbol_, encodedPinyin_);
    engine_->updateUI(inputContext);
}

SpellCandidateWord::SpellCandidateWord(PinyinEngine *engine, std::string word,
                                       size_t inputLength, int order)
    : PinyinAbstractExtraCandidateWordInterface(*this, order), engine_(engine),
      word_(std::move(word)), inputLength_(inputLength) {
    setText(Text(word_));
}

void SpellCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto &context = state->context_;
    context.selectCustom(inputLength_, word_);
    engine_->updateUI(inputContext);
}

PinyinCandidateWord::PinyinCandidateWord(PinyinEngine *engine,
                                         InputContext *inputContext, Text text,
                                         size_t idx)
    : CandidateWord(std::move(text)), engine_(engine),
      inputContext_(inputContext), idx_(idx) {}

void PinyinCandidateWord::select(InputContext *inputContext) const {
    if (inputContext != inputContext_) {
        return;
    }
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto &context = state->context_;
    if (idx_ >= context.candidatesToCursor().size()) {
        return;
    }
    context.selectCandidatesToCursor(idx_);
    engine_->updateUI(inputContext);
}

std::string PinyinCandidateWord::customPhraseString() const {
    auto *state = inputContext_->propertyFor(&engine_->factory());
    auto &context = state->context_;
    if (idx_ >= context.candidatesToCursor().size()) {
        return "";
    }
    const auto candidatePyLength =
        context.candidatesToCursor()[idx_].sentence().back()->to()->index();
    const auto selectedLength = state->context_.selectedLength();
    const auto currentSearch = (state->context_.cursor() == selectedLength)
                                   ? state->context_.size()
                                   : state->context_.cursor();
    if (currentSearch == candidatePyLength + selectedLength) {
        return text().toString();
    }
    return "";
}

CustomCloudPinyinCandidateWord::CustomCloudPinyinCandidateWord(
    PinyinEngine *engine, const std::string &pinyin,
    const std::string &selectedSentence, InputContext *inputContext,
    CloudPinyinSelectedCallback callback, int order)
    : CloudPinyinCandidateWord(engine->cloudpinyin(), pinyin, selectedSentence,
                               *engine->config().keepCloudPinyinPlaceHolder,
                               inputContext, std::move(callback)),
      PinyinAbstractExtraCandidateWordInterface(*this, order) {
    if (filled() || !*engine->config().cloudPinyinAnimation) {
        return;
    }
    setText(Text(std::string(ProgerssString[tick_])));
    // This should be high accuracy since it's per 120ms.
    timeEvent_ = engine->instance()->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + TickPeriod, 1000,
        [this, ref = this->watch()](EventSourceTime *, uint64_t time) {
            if (!ref.isValid()) {
                return true;
            }
            if (filled()) {
                timeEvent_.reset();
                return true;
            }
            tick_ = (time / TickPeriod) % ProgerssString.size();
            setText(Text(std::string(ProgerssString[tick_])));
            this->inputContext()->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            timeEvent_->setTime(timeEvent_->time() + TickPeriod);
            timeEvent_->setOneShot();
            return true;
        });
}

void CustomCloudPinyinCandidateWord::select(InputContext *inputContext) const {
    if ((!filled() || word().empty()) && order() == 0) {
        auto candidateList = inputContext->inputPanel().candidateList();
        for (int i = 0; i < candidateList->size(); i++) {
            if (&candidateList->candidate(i) != this) {
                return candidateList->candidate(i).select(inputContext);
            }
        }
    }
    CloudPinyinCandidateWord::select(inputContext);
}

enum { PINYIN_FORGET, PINYIN_CUSTOMPHRASE, PINYIN_DELETE_CUSTOMPHRASE };

PinyinActionableCandidateList::PinyinActionableCandidateList(
    PinyinEngine *engine, InputContext *inputContext)
    : engine_(engine), inputContext_(inputContext) {}

bool PinyinActionableCandidateList::hasAction(
    const CandidateWord &candidate) const {
    return isForgettable(candidate) || canBeInsertedAsCustomPhrase(candidate);
}

std::vector<CandidateAction> PinyinActionableCandidateList::candidateActions(
    const CandidateWord &candidate) const {
    std::vector<CandidateAction> result;
    if (isForgettable(candidate)) {
        CandidateAction action;
        action.setId(PINYIN_FORGET);
        action.setText(_("Forget candidate"));
        result.push_back(std::move(action));
    }
    // If it's not custom phrase, or order is not at the top.
    const auto *customPhrase =
        dynamic_cast<const CustomPhraseCandidateWord *>(&candidate);
    if (canBeInsertedAsCustomPhrase(candidate) &&
        (!customPhrase || customPhrase->order() != 0)) {
        CandidateAction action;
        action.setId(PINYIN_CUSTOMPHRASE);
        action.setText(_("Pin to top as custom phrase"));
        result.push_back(std::move(action));
    }
    if (isCustomPhrase(candidate)) {
        CandidateAction action;
        action.setId(PINYIN_DELETE_CUSTOMPHRASE);
        action.setText(_("Delete from custom phrase"));
        result.push_back(std::move(action));
    }

    return result;
}

void PinyinActionableCandidateList::triggerAction(
    const CandidateWord &candidate, int id) {
    switch (id) {
    case PINYIN_FORGET: {
        if (const auto *pinyinCandidate =
                dynamic_cast<const ForgettableCandidateInterface *>(
                    &candidate)) {
            engine_->forgetCandidate(inputContext_,
                                     pinyinCandidate->candidateIndex());
        }
    } break;
    case PINYIN_CUSTOMPHRASE: {
        if (const auto *insertable =
                dynamic_cast<const InsertableAsCustomPhraseInterface *>(
                    &candidate)) {
            auto customPhraseString = insertable->customPhraseString();
            if (!customPhraseString.empty()) {
                engine_->pinCustomPhrase(inputContext_, customPhraseString);
            }
        }
    } break;
    case PINYIN_DELETE_CUSTOMPHRASE: {
        if (const auto *customPhrase =
                dynamic_cast<const CustomPhraseCandidateWord *>(&candidate)) {

            auto customPhraseString = customPhrase->customPhraseString();
            if (!customPhraseString.empty()) {
                engine_->deleteCustomPhrase(inputContext_, customPhraseString);
            }
        }
    } break;
    default:
        break;
    }
}
} // namespace fcitx
