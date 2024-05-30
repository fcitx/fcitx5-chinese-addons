/*
 * SPDX-FileCopyrightText: 2017-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _TABLE_CANDIDATE_H_
#define _TABLE_CANDIDATE_H_

#include "engine.h"
#include <cstddef>
#include <fcitx/candidateaction.h>
#include <fcitx/candidatelist.h>
#include <fcitx/text.h>
#include <libime/table/tablebaseddictionary.h>
#include <string>
#include <vector>

namespace fcitx {

class TableCandidateWord : public CandidateWord {
public:
    TableCandidateWord(TableEngine *engine, Text text, size_t idx);

    void select(InputContext *inputContext) const override;

    TableEngine *engine_;
    size_t idx_;
};

class TablePinyinCandidateWord : public CandidateWord {
public:
    TablePinyinCandidateWord(TableEngine *engine, std::string word,
                             const libime::TableBasedDictionary &dict,
                             bool customHint);

    void select(InputContext *inputContext) const override;

    TableEngine *engine_;
    std::string word_;
};

class TablePunctuationCandidateWord : public CandidateWord {
public:
    TablePunctuationCandidateWord(TableState *state, std::string word,
                                  bool isHalf);

    void select(InputContext *inputContext) const override;

    const std::string &word() const { return word_; }

private:
    TableState *state_;
    std::string word_;
};

class TablePredictCandidateWord : public CandidateWord {
public:
    TablePredictCandidateWord(TableState *state, std::string word);

    void select(InputContext *inputContext) const override;

    TableState *state_;
    std::string word_;
};

class TableActionableCandidateList : public ActionableCandidateList {
public:
    TableActionableCandidateList(TableState *state);

    bool hasAction(const CandidateWord &candidate) const override;
    std::vector<CandidateAction>
    candidateActions(const CandidateWord &candidate) const override;
    void triggerAction(const CandidateWord &candidate, int id) override;

private:
    TableState *state_;
};

} // namespace fcitx

#endif // _TABLE_CANDIDATE_H_
