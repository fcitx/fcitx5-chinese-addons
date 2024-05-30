/*
 * SPDX-FileCopyrightText: 2017-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "candidate.h"
#include "engine.h"
#include "state.h"
#include <cstddef>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidateaction.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>
#include <libime/table/tablebaseddictionary.h>
#include <string>
#include <utility>
#include <vector>

namespace fcitx {

TableCandidateWord::TableCandidateWord(TableEngine *engine, Text text,
                                       size_t idx)
    : CandidateWord(std::move(text)), engine_(engine), idx_(idx) {}
void TableCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    // nullptr means use the last requested entry.
    auto *context = state->updateContext(nullptr);
    if (!context || idx_ >= context->candidates().size()) {
        return;
    }
    if (state->mode() == TableMode::ForgetWord) {
        state->forgetCandidateWord(idx_);
        return;
    }
    {
        const CommitAfterSelectWrapper commitAfterSelectRAII(state);
        context->select(idx_);
    }
    if (context->selected()) {
        state->commitBuffer(true);
    }
    state->updateUI(/*keepOldCursor=*/false, /*maybePredict=*/true);
}
TablePinyinCandidateWord::TablePinyinCandidateWord(
    TableEngine *engine, std::string word,
    const libime::TableBasedDictionary &dict, bool customHint)
    : engine_(engine), word_(std::move(word)) {
    setText(Text(word_));
    if (utf8::lengthValidated(word_) == 1) {
        if (auto code = dict.reverseLookup(word_); !code.empty()) {
            Text comment;
            comment.append("~ ");
            if (customHint) {
                comment.append(dict.hint(code));
            } else {
                comment.append(std::move(code));
            }
            setComment(std::move(comment));
        }
    }
}
void TablePinyinCandidateWord::select(InputContext *inputContext) const {
    auto *state = inputContext->propertyFor(&engine_->factory());
    inputContext->commitString(word_);
    state->pushLastCommit("", word_);
    state->resetAndPredict();
}
TablePunctuationCandidateWord::TablePunctuationCandidateWord(TableState *state,
                                                             std::string word,
                                                             bool isHalf)
    : state_(state), word_(std::move(word)) {
    setText(Text(word_));
    if (isHalf) {
        setComment(Text(_("(Half)")));
    }
}
void TablePunctuationCandidateWord::select(InputContext *inputContext) const {
    state_->commitBuffer(true);
    inputContext->commitString(word_);
    state_->reset();
}

TablePredictCandidateWord::TablePredictCandidateWord(TableState *state,
                                                     std::string word)
    : CandidateWord(Text(word)), state_(state), word_(std::move(word)) {}
void TablePredictCandidateWord::select(InputContext *inputContext) const {
    state_->commitBuffer(true);
    inputContext->commitString(word_);
    state_->pushLastCommit("", word_);
    state_->resetAndPredict();
}

TableActionableCandidateList::TableActionableCandidateList(TableState *state)
    : state_(state) {}

bool TableActionableCandidateList::hasAction(
    const CandidateWord &candidate) const {
    return dynamic_cast<const TableCandidateWord *>(&candidate);
}
std::vector<CandidateAction> TableActionableCandidateList::candidateActions(
    const CandidateWord &candidate) const {
    if (!hasAction(candidate)) {
        return {};
    }
    std::vector<CandidateAction> actions;
    CandidateAction action;
    action.setId(0);
    action.setText(_("Forget word"));
    actions.push_back(std::move(action));
    return actions;
}

void TableActionableCandidateList::triggerAction(const CandidateWord &candidate,
                                                 int id) {
    if (id != 0) {
        return;
    }
    if (const auto *tableCandidate =
            dynamic_cast<const TableCandidateWord *>(&candidate)) {
        state_->forgetCandidateWord(tableCandidate->idx_);
    }
}
} // namespace fcitx
