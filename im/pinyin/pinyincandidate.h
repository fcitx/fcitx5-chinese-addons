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
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx/candidateaction.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>
#include <libime/core/lattice.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fcitx {

class PinyinEngine;
class PinyinState;

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

class PinyinCandidateIndexInterface {
public:
    virtual ~PinyinCandidateIndexInterface();
    virtual int candidateIndex() const = 0;
};

using CandidateOrder = std::pair<size_t, size_t>;

class PinyinAbstractCandidateWord : virtual public CandidateWord {
public:
    explicit PinyinAbstractCandidateWord(size_t selectLength,
                                         CandidateOrder order);

    virtual ~PinyinAbstractCandidateWord();

    size_t order() const { return order_.first; };
    CandidateOrder sortOrder() const { return order_; };
    size_t selectLength() const { return selectLength_; }
    virtual bool isPinyinCandidate() const { return false; }
    virtual bool isCustomPhrase() const { return false; }

protected:
    const size_t selectLength_;
    const CandidateOrder order_;
};

class StrokeCandidateWord : public PinyinAbstractCandidateWord {
public:
    StrokeCandidateWord(PinyinEngine *engine, std::string hz,
                        const std::string &py, size_t selectLength,
                        CandidateOrder order);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    std::string hz_;
};

class CustomPhraseCandidateWord : public PinyinAbstractCandidateWord,
                                  public InsertableAsCustomPhraseInterface {
public:
    CustomPhraseCandidateWord(PinyinEngine *engine, size_t selectLength,
                              CandidateOrder order, std::string value,
                              std::string customPhraseString);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override {
        return customPhraseString_;
    }

    bool isCustomPhrase() const override { return true; }

private:
    PinyinEngine *engine_;
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
    LuaCandidateWord(PinyinEngine *engine, size_t selectLength,
                     std::string word);

    void select(InputContext *inputContext) const override;

private:
    PinyinEngine *engine_;
    size_t selectLength_ = 0;
    std::string word_;
};

class SymbolCandidateWord : public CandidateWord,
                            public InsertableAsCustomPhraseInterface,
                            public PinyinCandidateIndexInterface {
public:
    SymbolCandidateWord(PinyinEngine *engine, std::string symbol,
                        std::string encodedPinyin, size_t selectLength,
                        bool isFull, int pinyinCandidateIndex);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override;
    int candidateIndex() const override { return pinyinCandidateIndex_; }

private:
    PinyinEngine *engine_;
    std::string symbol_;
    size_t candidateSegmentLength_ = 0;
    const bool isFull_ = false;
    std::string encodedPinyin_;
    int pinyinCandidateIndex_;
};

class SpellCandidateWord : public PinyinAbstractCandidateWord,
                           public InsertableAsCustomPhraseInterface {
public:
    SpellCandidateWord(PinyinEngine *engine, std::string word,
                       size_t inputLength, CandidateOrder order);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override { return word_; }

private:
    PinyinEngine *engine_;
    std::string word_;
};

class PinyinCandidateWord : public PinyinAbstractCandidateWord,
                            public InsertableAsCustomPhraseInterface,
                            public PinyinCandidateIndexInterface {
public:
    PinyinCandidateWord(PinyinEngine *engine, InputContext *inputContext,
                        Text text, size_t selectLength, size_t idx,
                        CandidateOrder order);

    void select(InputContext *inputContext) const override;

    std::string customPhraseString() const override;
    int candidateIndex() const override { return idx_; }
    bool isPinyinCandidate() const override { return true; }

    bool isCustomPhrase() const override { return isCustomPhrase_; }
    void setCustomPhrase() { isCustomPhrase_ = true; }

private:
    PinyinEngine *engine_;
    InputContext *inputContext_;
    size_t idx_;
    bool isCustomPhrase_ = false;
};

class CustomCloudPinyinCandidateWord
    : public CloudPinyinCandidateWord,
      public PinyinAbstractCandidateWord,
      public InsertableAsCustomPhraseInterface {
public:
    CustomCloudPinyinCandidateWord(PinyinEngine *engine,
                                   const std::string &pinyin,
                                   const std::string &selectedSentence,
                                   InputContext *inputContext,
                                   CloudPinyinSelectedCallback callback,
                                   CandidateOrder order);

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
    static bool isPinyinCandidate(const CandidateWord &candidate) {
        return dynamic_cast<const PinyinCandidateWord *>(&candidate);
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
        if (const auto *pinyinCandidate =
                dynamic_cast<const PinyinAbstractCandidateWord *>(&candidate);
            pinyinCandidate && pinyinCandidate->isCustomPhrase()) {
            return true;
        }
        return false;
    }

    PinyinEngine *engine_;
    InputContext *inputContext_;
};

class PinyinTabbedCandidateList : public TabbedCandidateList {
public:
    PinyinTabbedCandidateList(PinyinEngine *engine, InputContext *inputContext,
                              CommonCandidateList *candidateList);

    std::span<const CandidateAction> tabActions() override;

    void triggerTabAction(int id) override;
    bool checked() const {
        return checkedPinyinActionId_.has_value() || checkedSingleAction_;
    }

    bool filter(const CandidateWord &candidate) const;

private:
    void triggerStrokeAction(PinyinState *state, int id);
    void triggerMainAction(PinyinState *state, int id);

    std::optional<int> idToActionIndex(int id) const;

    void buildTabActions();

    enum ActionId {
        SINGLE_ACTION = -1,
        STROKE_ACTION = -2,
        STROKE_SUB_ACTION_H = -3,
        STROKE_SUB_ACTION_S = -4,
        STROKE_SUB_ACTION_P = -5,
        STROKE_SUB_ACTION_N = -6,
        STROKE_SUB_ACTION_Z = -7,
        STROKE_SUB_ACTION_RETURN = -8,
        SEPARATOR_ACTION = -9,
    };

    PinyinEngine *engine_;
    InputContext *inputContext_;
    CommonCandidateList *candidateList_;

    // Lazily initialized actions, since it requires scan all actions.
    std::optional<std::vector<CandidateAction>> actions_;
    std::vector<CandidateAction> strokeActions_;
    std::optional<int> checkedPinyinActionId_ = std::nullopt;
    bool checkedSingleAction_ = false;
    std::vector<std::unordered_set<int>> actionIdToCandidates_;
};

} // namespace fcitx

#endif // _PINYIN_PINYINCANDIDATE_H_
