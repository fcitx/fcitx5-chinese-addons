/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _TABLE_STATE_H_
#define _TABLE_STATE_H_

#include "context.h"
#include "engine.h"
#include "ime.h"
#include <cstddef>
#include <fcitx-utils/inputbuffer.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodentry.h>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fcitx {

enum class TableMode {
    Normal,
    ModifyDictionary,
    ForgetWord,
    Pinyin,
    LookupPinyin,
    Punctuation,
};

struct EventSourceTime;

class TableState : public InputContextProperty {
public:
    TableState(InputContext *ic, TableEngine *engine)
        : ic_(ic), engine_(engine) {}

    InputContext *ic_;
    TableEngine *engine_;
    bool lastIsPunc_ = false;
    std::unique_ptr<EventSourceTime> cancelLastEvent_;

    TableContext *updateContext(const InputMethodEntry *entry);
    void release();
    void reset(const InputMethodEntry *entry = nullptr);
    void resetAndPredict();
    void predict();

    void keyEvent(const InputMethodEntry &entry, KeyEvent &event);

    void commitBuffer(bool commitCode, bool noRealCommit = false);
    void updateUI(bool keepOldCursor, bool maybePredict);
    void updatePuncCandidate(InputContext *inputContext,
                             const std::string &original,
                             const std::vector<std::string> &candidates);
    void updatePuncPreedit(InputContext *inputContext);
    void pushLastCommit(const std::string &code,
                        const std::string &lastSegment);

    void commitAfterSelect(int commitFrom);

    auto mode() const { return mode_; }

    void forgetCandidateWord(size_t idx);
    void handle2nd3rdCandidate(KeyEvent &event) {
        auto *context = context_.get();
        if (!context) {
            return;
        }

        const auto &config = context->config();
        handle2nd3rdCandidate(config, event);
    }

    auto context() const { return context_.get(); }

private:
    bool handle2nd3rdCandidate(const TableConfig &config, KeyEvent &event);
    bool handlePuncCandidate(const TableConfig &config, KeyEvent &event);
    bool handleCandidateList(const TableConfig &config, KeyEvent &event);
    bool handleForgetWord(KeyEvent &event);
    bool handlePinyinMode(KeyEvent &event);
    bool handleLookupPinyinOrModifyDictionaryMode(KeyEvent &event);

    bool isContextEmpty() const;
    bool autoSelectCandidate() const;
    std::unique_ptr<CandidateList>
    predictCandidateList(const std::vector<std::string> &words);
    std::string commitSegements(size_t from, size_t to);

    TableMode mode_ = TableMode::Normal;
    std::string pinyinModePrefix_;
    InputBuffer pinyinModeBuffer_{
        {InputBufferOption::AsciiOnly, InputBufferOption::FixedCursor}};
    size_t lookupPinyinIndex_ = 0;
    std::vector<std::pair<std::string, std::string>> lookupPinyinString_;
    std::string lastContext_;
    std::list<std::pair<std::string, std::string>> lastCommit_;
    std::string lastSegment_;
    std::list<std::pair<std::string, std::string>> autoPhraseBuffer_;
    std::unique_ptr<TableContext> context_;

    int keyReleased_ = -1;
    int keyReleasedIndex_ = -2;
};

class CommitAfterSelectWrapper {
public:
    CommitAfterSelectWrapper(TableState *state) : state_(state) {
        if (auto *context = state->updateContext(nullptr)) {
            commitFrom_ = static_cast<int>(context->selectedSize());
        }
    }

    ~CommitAfterSelectWrapper() {
        if (commitFrom_ >= 0) {
            state_->commitAfterSelect(commitFrom_);
        }
    }

private:
    TableState *state_;
    int commitFrom_ = -1;
};

} // namespace fcitx

#endif // _TABLE_STATE_H_
