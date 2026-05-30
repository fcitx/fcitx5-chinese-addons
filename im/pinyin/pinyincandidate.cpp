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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx/candidateaction.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <libime/core/historybigram.h>
#include <libime/core/lattice.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fcitx {

namespace {

// Helper function to produce the full pinyin string that matches the best to
// the encoded candidate pinyin.
std::string bestMatchPinyin(const std::string &pinyin,
                            const std::string &candidatePinyin,
                            libime::PinyinContext &context) {

    libime::MatchedPinyinSyllablesWithFuzzyFlags syls;
    syls = context.useShuangpin()
               ? libime::PinyinEncoder::shuangpinToSyllablesWithFuzzyFlags(
                     pinyin, *context.ime()->shuangpinProfile(),
                     context.ime()->fuzzyFlags())
               : libime::PinyinEncoder::stringToSyllablesWithFuzzyFlags(
                     pinyin, context.ime()->correctionProfile().get(),
                     context.ime()->fuzzyFlags());
    std::string actualPinyin;
    std::optional<libime::PinyinSyllable> syl;

    if (!syls.empty() && !syls.front().second.empty() &&
        candidatePinyin.size() >= 2) {
        auto candidateInitial =
            static_cast<libime::PinyinInitial>(candidatePinyin[0]);
        auto candidateFinal =
            static_cast<libime::PinyinFinal>(candidatePinyin[1]);

        for (const auto &initial : syls) {
            for (const auto &[final, fuzzy] : initial.second) {
                if (candidateInitial == initial.first) {
                    if (candidateFinal == final ||
                        (final == libime::PinyinFinal::Invalid && !syl)) {
                        syl.emplace(initial.first, final);
                    }
                }
            }
        }
    }

    if (syl) {
        actualPinyin = libime::PinyinEncoder::initialFinalToPinyinString(
            syl->initial(), syl->final());
    } else {
        actualPinyin = pinyin;
    }
    return actualPinyin;
}

bool isSinglePinyin(const libime::PinyinContext &context, size_t idx) {
    if (idx >= context.candidatesToCursor().size()) {
        return false;
    }
    const auto &result = context.candidatesToCursor()[idx];
    size_t totalSize = 0;
    for (const auto &node : result.sentence()) {
        totalSize +=
            node->as<libime::PinyinLatticeNode>().encodedPinyin().size();
        if (totalSize > 2) {
            return false;
        }
    }
    return totalSize == 2;
}

} // namespace

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
    predictWords.push_back({word_, ""});
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
        state->predictWords_->back().first.append(word_);
        state->predictWords_->back().second.clear();
    }
    engine_->updatePredict(inputContext);
}

InsertableAsCustomPhraseInterface::~InsertableAsCustomPhraseInterface() =
    default;

PinyinCandidateIndexInterface::~PinyinCandidateIndexInterface() = default;

PinyinAbstractCandidateWord::PinyinAbstractCandidateWord(size_t selectLength,
                                                         CandidateOrder order)
    : selectLength_(selectLength), order_(std::move(order)) {}

PinyinAbstractCandidateWord::~PinyinAbstractCandidateWord() = default;

StrokeCandidateWord::StrokeCandidateWord(PinyinEngine *engine, std::string hz,
                                         const std::string &py,
                                         size_t selectLength,
                                         CandidateOrder order)
    : PinyinAbstractCandidateWord(selectLength, order), engine_(engine),
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
    PinyinEngine *engine, size_t selectLength, CandidateOrder order,
    std::string value, std::string customPhraseString)
    : PinyinAbstractCandidateWord(selectLength, order), engine_(engine),
      customPhraseString_(std::move(customPhraseString)) {
    setText(Text(std::move(value)));
}

void CustomPhraseCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto &context = state->context_;
    context.selectCustom(selectLength_, text().toString());
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

ForgetCandidateWord::ForgetCandidateWord(PinyinEngine *engine, Text text,
                                         size_t index)
    : engine_(engine), index_(index) {
    setText(std::move(text));
}

void ForgetCandidateWord::select(InputContext *inputContext) const {
    engine_->forgetCandidate(inputContext, index_);
}

LuaCandidateWord::LuaCandidateWord(PinyinEngine *engine, size_t selectLength,
                                   std::string word)
    : engine_(engine), selectLength_(selectLength), word_(std::move(word)) {
    setText(Text(word_));
}

void LuaCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto segmentLength =
        state->context_.size() - state->context_.selectedLength();
    segmentLength = std::min(segmentLength, selectLength_);
    state->context_.selectCustom(segmentLength, word_);
    engine_->updateUI(inputContext);
}

SymbolCandidateWord::SymbolCandidateWord(PinyinEngine *engine,
                                         std::string symbol,
                                         std::string encodedPinyin,
                                         size_t selectLength, bool isFull,
                                         int pinyinCandidateIndex)
    : engine_(engine), symbol_(std::move(symbol)),
      candidateSegmentLength_(selectLength), isFull_(isFull),
      encodedPinyin_(std::move(encodedPinyin)),
      pinyinCandidateIndex_(pinyinCandidateIndex) {
    setText(Text(symbol_));
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
                                       size_t inputLength, CandidateOrder order)
    : PinyinAbstractCandidateWord(inputLength, order), engine_(engine),
      word_(std::move(word)) {
    setText(Text(word_));
}

void SpellCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    auto &context = state->context_;
    context.selectCustom(selectLength_, word_);
    engine_->updateUI(inputContext);
}

PinyinCandidateWord::PinyinCandidateWord(PinyinEngine *engine,
                                         InputContext *inputContext, Text text,
                                         size_t selectLength, size_t idx,
                                         CandidateOrder order)
    : PinyinAbstractCandidateWord(selectLength, order), engine_(engine),
      inputContext_(inputContext), idx_(idx) {
    setText(std::move(text));
}

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
    CloudPinyinSelectedCallback callback, CandidateOrder order)
    : CloudPinyinCandidateWord(engine->cloudpinyin(), pinyin, selectedSentence,
                               *engine->config().keepCloudPinyinPlaceHolder,
                               inputContext, std::move(callback)),
      PinyinAbstractCandidateWord(pinyin.size(), order) {
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
                candidateList->candidate(i).select(inputContext);
                return;
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
    return isPinyinCandidate(candidate) ||
           canBeInsertedAsCustomPhrase(candidate);
}

std::vector<CandidateAction> PinyinActionableCandidateList::candidateActions(
    const CandidateWord &candidate) const {
    std::vector<CandidateAction> result;
    if (isPinyinCandidate(candidate)) {
        CandidateAction action;
        action.setId(PINYIN_FORGET);
        action.setText(_("Forget candidate"));
        result.push_back(std::move(action));
    }
    // If it's not custom phrase, or order is not at the top.
    const auto *customPhrase =
        dynamic_cast<const PinyinAbstractCandidateWord *>(&candidate);
    if (canBeInsertedAsCustomPhrase(candidate) &&
        (!customPhrase || !customPhrase->isCustomPhrase() ||
         customPhrase->order() != 0)) {
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
                dynamic_cast<const PinyinCandidateWord *>(&candidate)) {
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
                dynamic_cast<const PinyinAbstractCandidateWord *>(&candidate);
            customPhrase && customPhrase->isCustomPhrase()) {
            if (const auto *insertable =
                    dynamic_cast<const InsertableAsCustomPhraseInterface *>(
                        &candidate)) {
                auto customPhraseString = insertable->customPhraseString();
                if (!customPhraseString.empty()) {
                    engine_->deleteCustomPhrase(inputContext_,
                                                customPhraseString);
                }
            }
        }
    } break;
    default:
        break;
    }
}

PinyinTabbedCandidateList::PinyinTabbedCandidateList(
    PinyinEngine *engine, InputContext *inputContext,
    CommonCandidateList *candidateList, std::optional<int> checkedActionId)
    : engine_(engine), inputContext_(inputContext),
      candidateList_(candidateList), checkedActionId_(checkedActionId) {}

std::span<const CandidateAction> PinyinTabbedCandidateList::tabActions() {
    auto *state = inputContext_->propertyFor(&engine_->factory());
    if (state->mode_ == PinyinMode::StrokeFilter) {
        return strokeActions_;
    }
    if (!actions_) {
        buildTabActions();
    }
    return *actions_;
}

void PinyinTabbedCandidateList::buildTabActions() {
    if (actions_) {
        return;
    }
    std::vector<CandidateAction> actions;
    actionIdToCandidates_.clear();

    auto *state = inputContext_->propertyFor(&engine_->factory());
    auto &context = state->context_;
    auto selectedLen = context.selectedLength();
    const auto &fullInput = context.userInput();

    std::map<std::tuple<std::string, std::string>, int> syllableToId;

    if (!candidateList_) {
        actions_ = std::move(actions);
        return;
    }

    for (size_t i = 0; i < candidateList_->originSize(); i++) {
        const auto *candidate = &candidateList_->originCandidate(i);
        const auto *pinyinCandidate =
            dynamic_cast<const PinyinCandidateWord *>(candidate);
        if (!pinyinCandidate || !pinyinCandidate->isPinyinCandidate()) {
            continue;
        }
        if (static_cast<size_t>(pinyinCandidate->candidateIndex()) >=
            context.candidatesToCursor().size()) {
            continue;
        }
        const auto &result =
            context.candidatesToCursor()[pinyinCandidate->candidateIndex()];
        const auto &sentence = result.sentence();
        if (sentence.empty()) {
            continue;
        }
        const auto &path = sentence[0]->path();
        if (path.size() < 2) {
            continue;
        }
        auto start = selectedLen + path[0]->index();
        auto end = selectedLen + path[1]->index();

        auto syllable = fullInput.substr(start, end - start);
        auto actualPinyin = bestMatchPinyin(
            syllable,
            sentence[0]->as<libime::PinyinLatticeNode>().encodedPinyin(),
            context);

        auto [it, inserted] = syllableToId.emplace(
            std::tuple{syllable, actualPinyin}, actions.size());
        if (inserted) {
            CandidateAction action;
            action.setId(actions.size());
            action.setText(actualPinyin);
            action.setCheckable(true);
            actions.push_back(std::move(action));
            actionIdToCandidates_.emplace_back();
        }
        actionIdToCandidates_[it->second].insert(
            pinyinCandidate->candidateIndex());
    }

    if (actions.size() <= 1) {
        actions.clear();
    }

    CandidateAction action;
    action.setId(SINGLE_ACTION);
    action.setText("单字");
    action.setCheckable(true);
    actions.push_back(std::move(action));

    CandidateAction stroke;
    stroke.setId(STROKE_ACTION);
    stroke.setText("笔画");
    actions.push_back(std::move(stroke));

    actions_ = std::move(actions);

    strokeActions_.clear();
    strokeActions_.emplace_back();
    strokeActions_.back().setId(STROKE_SUB_ACTION_H);
    strokeActions_.back().setText("一");
    strokeActions_.emplace_back();
    strokeActions_.back().setId(STROKE_SUB_ACTION_S);
    strokeActions_.back().setText("丨");
    strokeActions_.emplace_back();
    strokeActions_.back().setId(STROKE_SUB_ACTION_P);
    strokeActions_.back().setText("ノ");
    strokeActions_.emplace_back();
    strokeActions_.back().setId(STROKE_SUB_ACTION_N);
    strokeActions_.back().setText("㇏");
    strokeActions_.emplace_back();
    strokeActions_.back().setId(STROKE_SUB_ACTION_Z);
    strokeActions_.back().setText("𠃍");
    strokeActions_.emplace_back();
    strokeActions_.back().setId(STROKE_SUB_ACTION_RETURN);
    strokeActions_.back().setText("返回");
}

void PinyinTabbedCandidateList::triggerTabAction(int id) {

    auto currentCandidateList = inputContext_->inputPanel().candidateList();
    if (!currentCandidateList || currentCandidateList.get() != candidateList_) {
        return;
    }

    auto *state = inputContext_->propertyFor(&engine_->factory());
    if (state->mode_ == PinyinMode::StrokeFilter) {
        triggerStrokeAction(state, id);
    } else {
        triggerMainAction(state, id);
    }
}

void PinyinTabbedCandidateList::triggerStrokeAction(PinyinState *state,
                                                    int id) {
    assert(state->mode_ == PinyinMode::StrokeFilter);

    switch (id) {
    case STROKE_SUB_ACTION_H:
    case STROKE_SUB_ACTION_S:
    case STROKE_SUB_ACTION_P:
    case STROKE_SUB_ACTION_N:
    case STROKE_SUB_ACTION_Z:
        state->strokeBuffer_.type(STROKE_SUB_ACTION_H - id + '1');
        break;
    case STROKE_SUB_ACTION_RETURN:
        engine_->resetStroke(inputContext_);
        break;
    default:
        return;
    }
    engine_->updateFilter(inputContext_);
}

std::optional<int> PinyinTabbedCandidateList::idToActionIndex(int id) const {
    assert(actions_.has_value() && actions_->size() >= 2);
    if (id < 0) {
        if (id == SINGLE_ACTION) {
            return actions_->size() - 2;
        }
    } else if (id >= 0 && id < static_cast<int>(actions_->size())) {
        return id;
    }
    return std::nullopt;
}

void PinyinTabbedCandidateList::triggerMainAction(PinyinState *state, int id) {
    if (!actions_) {
        return;
    }
    std::optional<int> checkableActionIndex;
    // negative id is special action.
    if (id == STROKE_ACTION) {
        state->mode_ = PinyinMode::StrokeFilter;
    } else if (checkableActionIndex = idToActionIndex(id);
               !checkableActionIndex) {
        return;
    }

    // If triggered action is checkable, update the action checked state
    // correspondingly.
    if (checkableActionIndex) {
        if (checkedActionId_) {
            if (auto oldIndex = idToActionIndex(*checkedActionId_)) {
                (*actions_)[*oldIndex].setChecked(false);
            }
        }

        if (checkedActionId_ == id) {
            // If the same action is triggered, uncheck it.
            checkedActionId_.reset();
        } else {
            checkedActionId_ = id;
            (*actions_)[*checkableActionIndex].setChecked(true);
        }
    }
    engine_->updateFilter(inputContext_);
}

bool PinyinTabbedCandidateList::filter(const CandidateWord &candidate) const {
    if (!checkedActionId_.has_value()) {
        return true;
    }

    const auto *pinyinCandidate =
        dynamic_cast<const PinyinCandidateIndexInterface *>(&candidate);
    if (!pinyinCandidate) {
        return false;
    }

    if (checkedActionId_ == SINGLE_ACTION) {
        auto *state = inputContext_->propertyFor(&engine_->factory());
        auto &context = state->context_;
        return isSinglePinyin(context, pinyinCandidate->candidateIndex());
    }

    const auto &candidateSet = actionIdToCandidates_[checkedActionId_.value()];
    return candidateSet.contains(pinyinCandidate->candidateIndex());
}

} // namespace fcitx
