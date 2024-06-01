/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYIN_PINYINCANDIDATE_H_
#define _PINYIN_PINYINCANDIDATE_H_

#include "../../modules/cloudpinyin/cloudpinyin_public.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcitx-utils/event.h>
#include <fcitx/candidateaction.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>
#include <libime/core/lattice.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

class InsertableAsCustomPhraseInterface {
public:
    virtual ~InsertableAsCustomPhraseInterface();
    virtual std::string customPhraseString() const = 0;
};

class ForgettableCandidateInterface {
public:
    virtual ~ForgettableCandidateInterface();
    virtual size_t candidateIndex() const = 0;
};

class PinyinAbstractExtraCandidateWordInterface {
public:
    explicit PinyinAbstractExtraCandidateWordInterface(CandidateWord &cand,
                                                       int order);

    virtual ~PinyinAbstractExtraCandidateWordInterface();

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
      public PinyinAbstractExtraCandidateWordInterface,
      public InsertableAsCustomPhraseInterface {
public:
    CustomPhraseCandidateWord(PinyinEngine *engine, int order,
                              size_t inputLength, std::string value,
                              std::string customPhraseString);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override {
        return customPhraseString_;
    }

private:
    PinyinEngine *engine_;
    size_t inputLength_;
    std::string customPhraseString_;
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

class StrokeFilterCandidateWord : public CandidateWord,
                                  public InsertableAsCustomPhraseInterface,
                                  public ForgettableCandidateInterface {
public:
    StrokeFilterCandidateWord(PinyinEngine *engine, InputContext *inputContext,
                              Text text, int index);

    void select(InputContext *inputContext) const override;
    std::string customPhraseString() const override;
    size_t candidateIndex() const override;

private:
    PinyinEngine *engine_;
    InputContext *inputContext_;
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

class LuaCandidateWord : public CandidateWord {
public:
    LuaCandidateWord(PinyinEngine *engine, std::string word);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    std::string word_;
};

class SymbolCandidateWord : public CandidateWord,
                            public InsertableAsCustomPhraseInterface {
public:
    SymbolCandidateWord(PinyinEngine *engine, std::string symbol,
                        const libime::SentenceResult &result, bool isFull);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override;

private:
    PinyinEngine *engine_;
    std::string symbol_;
    size_t candidateSegmentLength_ = 0;
    const bool isFull_ = false;
    std::string encodedPinyin_;
};

class SpellCandidateWord : public CandidateWord,
                           public PinyinAbstractExtraCandidateWordInterface,
                           public InsertableAsCustomPhraseInterface {
public:
    SpellCandidateWord(PinyinEngine *engine, std::string word,
                       size_t inputLength, int order);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override { return word_; }

private:
    PinyinEngine *engine_;
    std::string word_;
    size_t inputLength_;
};

class PinyinCandidateWord : public CandidateWord,
                            public InsertableAsCustomPhraseInterface,
                            public ForgettableCandidateInterface {
public:
    PinyinCandidateWord(PinyinEngine *engine, InputContext *inputContext,
                        Text text, size_t idx);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override;
    size_t candidateIndex() const override { return idx_; }

    PinyinEngine *engine_;
    InputContext *inputContext_;
    size_t idx_;
};

class CustomCloudPinyinCandidateWord
    : public CloudPinyinCandidateWord,
      public PinyinAbstractExtraCandidateWordInterface,
      public InsertableAsCustomPhraseInterface {
public:
    CustomCloudPinyinCandidateWord(PinyinEngine *engine,
                                   const std::string &pinyin,
                                   const std::string &selectedSentence,
                                   InputContext *inputContext,
                                   CloudPinyinSelectedCallback callback,
                                   int order);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override {
        return filled() ? word() : "";
    }

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

class PinyinActionableCandidateList : public ActionableCandidateList {
public:
    PinyinActionableCandidateList(PinyinEngine *engine,
                                  InputContext *inputContext);

    bool hasAction(const CandidateWord &candidate) const override;

    std::vector<CandidateAction>
    candidateActions(const CandidateWord &candidate) const override;

    void triggerAction(const CandidateWord &candidate, int id) override;

private:
    static bool isForgettable(const CandidateWord &candidate) {
        return dynamic_cast<const ForgettableCandidateInterface *>(&candidate);
    }

    static bool canBeInsertedAsCustomPhrase(const CandidateWord &candidate) {
        if (const auto *insertable =
                dynamic_cast<const InsertableAsCustomPhraseInterface *>(
                    &candidate)) {
            return !insertable->customPhraseString().empty();
        }
        return false;
    }

    static bool isCustomPhrase(const CandidateWord &candidate) {
        return dynamic_cast<const CustomPhraseCandidateWord *>(&candidate);
    }

    PinyinEngine *engine_;
    InputContext *inputContext_;
};

} // namespace fcitx

#endif // _PINYIN_PINYINCANDIDATE_H_
