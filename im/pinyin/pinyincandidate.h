/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYIN_PINYINCANDIDATE_H_
#define _PINYIN_PINYINCANDIDATE_H_

#include "../modules/cloudpinyin/cloudpinyin_public.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcitx-utils/event.h>
#include <fcitx/candidatelist.h>
#include <fcitx/text.h>
#include <libime/core/lattice.h>
#include <memory>
#include <string>
#include <string_view>

namespace fcitx {

class PinyinEngine;

class PinyinPredictCandidateWord : public CandidateWord {
public:
    PinyinPredictCandidateWord(PinyinEngine *engine, std::string word);

    void select(InputContext *inputContext) const override;

    PinyinEngine *engine_;
    std::string word_;
};

class PinyinPredictDictCandidateWord : public CandidateWord {
public:
    PinyinPredictDictCandidateWord(PinyinEngine *engine, std::string word);

    void select(InputContext *inputContext) const override;

    PinyinEngine *engine_;
    std::string word_;
};

class PinyinAbstractExtraCandidateWordInterface {
public:
    explicit PinyinAbstractExtraCandidateWordInterface(CandidateWord &cand,
                                                       int order);

    virtual ~PinyinAbstractExtraCandidateWordInterface() = default;

    int order() const { return order_; };
    const CandidateWord &toCandidateWord() const { return cand_; }
    CandidateWord &toCandidateWord() { return cand_; }

private:
    CandidateWord &cand_;
    int order_;
};

class StrokeCandidateWord : public CandidateWord,
                            public PinyinAbstractExtraCandidateWordInterface {
public:
    StrokeCandidateWord(PinyinEngine *engine, std::string hz,
                        const std::string &py, int order);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    std::string hz_;
};

class CustomPhraseCandidateWord
    : public CandidateWord,
      public PinyinAbstractExtraCandidateWordInterface {
public:
    CustomPhraseCandidateWord(const PinyinEngine *engine, int order,
                              std::string value);

    void select(InputContext *inputContext) const override;

private:
    const PinyinEngine *engine_;
};

class PinyinPunctuationCandidateWord : public CandidateWord {
public:
    PinyinPunctuationCandidateWord(const PinyinEngine *engine, std::string word,
                                   bool isHalf);

    void select(InputContext *inputContext) const override;

    const std::string &word() const { return word_; }

private:
    const PinyinEngine *engine_;
    std::string word_;
};

class StrokeFilterCandidateWord : public CandidateWord {
public:
    StrokeFilterCandidateWord(PinyinEngine *engine, Text text, int index);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    int index_;
};

class ForgetCandidateWord : public CandidateWord {
public:
    ForgetCandidateWord(PinyinEngine *engine, Text text, size_t index);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    size_t index_;
};

class ExtraCandidateWord : public CandidateWord {
public:
    ExtraCandidateWord(PinyinEngine *engine, std::string word);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    std::string word_;
};

class SymbolCandidateWord : public CandidateWord {
public:
    SymbolCandidateWord(PinyinEngine *engine, std::string symbol,
                        const libime::SentenceResult &result);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    std::string symbol_;
    size_t candidateSegmentLength_ = 0;
    std::string encodedPinyin_;
};

class SpellCandidateWord : public CandidateWord,
                           public PinyinAbstractExtraCandidateWordInterface {
public:
    SpellCandidateWord(PinyinEngine *engine, std::string word,
                       size_t inputLength, int order);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    std::string word_;
    size_t inputLength_;
};

class PinyinCandidateWord : public CandidateWord {
public:
    PinyinCandidateWord(PinyinEngine *engine, Text text, size_t idx);

    void select(InputContext *inputContext) const override;

    PinyinEngine *engine_;
    size_t idx_;
};

class CustomCloudPinyinCandidateWord
    : public CloudPinyinCandidateWord,
      public PinyinAbstractExtraCandidateWordInterface {
public:
    CustomCloudPinyinCandidateWord(PinyinEngine *engine,
                                   const std::string &pinyin,
                                   const std::string &selectedSentence,
                                   InputContext *inputContext,
                                   CloudPinyinSelectedCallback callback,
                                   int order);

    void select(InputContext *inputContext) const override;

private:
    static constexpr std::array<std::string_view, 4> ProgerssString = {
        "◐",
        "◓",
        "◑",
        "◒",
    };
    int tick_ = (now(CLOCK_MONOTONIC) / TickPeriod) % ProgerssString.size();
    std::unique_ptr<EventSourceTime> timeEvent_;
    static constexpr uint64_t TickPeriod = 180000;
};

} // namespace fcitx

#endif // _PINYIN_PINYINCANDIDATE_H_
