/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "state.h"
#include "pinyinhelper_public.h"
#include "punctuation_public.h"
#include "quickphrase_public.h"
#include <fcitx-utils/event.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fmt/format.h>
#include <libime/core/historybigram.h>
#include <libime/pinyin/pinyinencoder.h>

namespace fcitx {

namespace {

class CommitAfterSelectWrapper {
public:
    CommitAfterSelectWrapper(TableState *state) : state_(state) {
        if (auto *context = state->updateContext(nullptr)) {
            commitFrom_ = context->selectedSize();
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

class TableCandidateWord : public CandidateWord {
public:
    TableCandidateWord(TableEngine *engine, Text text, size_t idx)
        : CandidateWord(std::move(text)), engine_(engine), idx_(idx) {}

    void select(InputContext *inputContext) const override {
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
            CommitAfterSelectWrapper commitAfterSelectRAII(state);
            context->select(idx_);
        }
        if (context->selected()) {
            state->commitBuffer(true);
        }
        state->updateUI();
    }

    TableEngine *engine_;
    size_t idx_;
};

class TablePinyinCandidateWord : public CandidateWord {
public:
    TablePinyinCandidateWord(TableEngine *engine, std::string word,
                             const libime::TableBasedDictionary &dict,
                             bool customHint)
        : engine_(engine), word_(std::move(word)) {
        Text text;
        text.append(word_);
        if (utf8::lengthValidated(word_) == 1) {
            auto code = dict.reverseLookup(word_);
            if (!code.empty()) {
                text.append(" ~ ");
                if (customHint) {
                    text.append(dict.hint(code));
                } else {
                    text.append(code);
                }
            }
        }
        setText(std::move(text));
    }

    void select(InputContext *inputContext) const override {
        auto *state = inputContext->propertyFor(&engine_->factory());
        inputContext->commitString(word_);
        state->pushLastCommit("", word_);
        state->reset();
    }

    TableEngine *engine_;
    std::string word_;
};
} // namespace

TableContext *TableState::updateContext(const InputMethodEntry *entry) {
    if (!entry || lastContext_ == entry->uniqueName()) {
        return context_.get();
    }

    auto dict = engine_->ime()->requestDict(entry->uniqueName());
    if (!std::get<0>(dict)) {
        return nullptr;
    }
    context_ = std::make_unique<TableContext>(
        *std::get<0>(dict), *std::get<2>(dict), *std::get<1>(dict));
    lastContext_ = entry->uniqueName();
    return context_.get();
}

void TableState::release() {
    reset();
    lastContext_.clear();
    context_.reset();
}

std::string TableState::commitSegements(size_t from, size_t to) {
    auto *context = context_.get();
    if (!context) {
        return "";
    }

    std::string sentence;
    const auto &config = context->config();
    for (size_t i = from; i < to; i++) {
        auto seg = context->selectedSegment(i);
        if (std::get<bool>(seg) || *config.commitInvalidSegment) {
            std::string code;
            const auto &word = std::get<std::string>(seg);
            if (utf8::length(word) == 1) {
                code = context->selectedCode(i);
            }
            pushLastCommit(code, word);
            sentence += word;
        }
    }
    return sentence;
}

void TableState::pushLastCommit(const std::string &code,
                                const std::string &lastSegment) {
    if (lastSegment.empty() ||
        ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) {
        return;
    }

    TABLE_DEBUG() << "TableState::pushLastCommit " << lastSegment
                  << " code: " << code;
    constexpr size_t limit = 10;
    const auto length = utf8::length(lastSegment);
    if (length == 1) {
        lastSingleCharCommit_.emplace_back(code, lastSegment);
        while (lastSingleCharCommit_.size() > limit) {
            lastSingleCharCommit_.pop_front();
        }

        std::string singleCharString;
        std::vector<std::string> codeHints;
        for (const auto &[code, chr] : lastSingleCharCommit_) {
            singleCharString += chr;
            codeHints.push_back(code);
        }
        TABLE_DEBUG() << "learnAutoPhrase " << singleCharString << codeHints;
        context_->learnAutoPhrase(singleCharString, codeHints);
    } else {
        lastSingleCharCommit_.clear();
    }

    if (length == 1) {
        lastCommit_.emplace_back(code, lastSegment);
    } else {
        auto range = fcitx::utf8::MakeUTF8CharRange(lastSegment);
        for (auto iter = std::begin(range); iter != std::end(range); iter++) {
            lastCommit_.emplace_back("", iter.view());
        }
    }

    if (lastCommit_.size() > limit) {
        while (lastCommit_.size() > limit) {
            lastCommit_.pop_front();
        }
    }
    lastSegment_ = lastSegment;
}

void TableState::reset(const InputMethodEntry *entry) {
    auto *context = updateContext(entry);
    if (context) {
        context->clear();
    }
    ic_->inputPanel().reset();
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    // lastIsPunc_ = false;
    mode_ = TableMode::Normal;
    pinyinModePrefix_.clear();
    pinyinModeBuffer_.clear();

    keyReleased_ = -1;
    keyReleasedIndex_ = -2;
}

bool TableState::isContextEmpty() const {
    if (!context_) {
        return true;
    }

    if (*context_->config().commitAfterSelect) {
        return context_->empty() || context_->selected();
    }
    return context_->empty();
}

bool TableState::autoSelectCandidate() {
    auto candidateList = ic_->inputPanel().candidateList();
    if (candidateList && candidateList->size()) {
        int idx = candidateList->cursorIndex();
        if (idx < 0) {
            idx = 0;
        }
        candidateList->candidate(idx).select(ic_);
        return true;
    }
    return false;
}

bool TableState::handleCandidateList(const TableConfig &config,
                                     KeyEvent &event) {
    auto *inputContext = event.inputContext();
    // check if we can select candidate.
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList) {
        return false;
    }

    int idx = event.key().keyListIndex(*config.selection);
    if (idx >= 0) {
        event.filterAndAccept();
        if (idx < candidateList->size()) {
            candidateList->candidate(idx).select(inputContext);
        }
        return true;
    }

    if (event.key().checkKeyList(*config.prevPage)) {
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

    if (event.key().checkKeyList(*config.nextPage)) {
        event.filterAndAccept();
        candidateList->toPageable()->next();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        return true;
    }
    if (auto *movable = candidateList->toCursorMovable()) {
        if (event.key().checkKeyList(*config.nextCandidate)) {
            movable->nextCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
        if (event.key().checkKeyList(*config.prevCandidate)) {
            movable->prevCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
    }

    return false;
}

bool TableState::handlePinyinMode(KeyEvent &event) {
    auto *context = context_.get();
    const auto &pinyinKey = *context->config().pinyinKey;
    if (pinyinKey.sym() == FcitxKey_None) {
        return false;
    }
    bool needUpdate = false;
    if (mode_ == TableMode::Normal && event.key().check(pinyinKey)) {
        auto chr = Key::keySymToUnicode(event.key().sym());
        if (!isContextEmpty()) {
            if (context->isValidInput(chr)) {
                return false;
            }
        }
        commitBuffer(true);
        mode_ = TableMode::Pinyin;
        event.filterAndAccept();

        if (!event.key().hasModifier()) {
            pinyinModePrefix_ = Key::keySymToUTF8(event.key().sym());
            needUpdate = true;
        }
    } else if (mode_ != TableMode::Pinyin) {
        return false;
    } else {
        event.filterAndAccept();
        if (event.key().isLAZ() || event.key().check(FcitxKey_apostrophe)) {
            pinyinModeBuffer_.type(Key::keySymToUTF8(event.key().sym()));
            needUpdate = true;
        } else if (event.key().check(FcitxKey_BackSpace)) {
            if (!pinyinModeBuffer_.empty()) {
                pinyinModeBuffer_.backspace();
                needUpdate = true;
            } else {
                reset();
                return true;
            }
        } else if (event.key().check(FcitxKey_space)) {
            if (autoSelectCandidate()) {
                return true;
            }
            if (pinyinModeBuffer_.empty()) {
                if (!lastSegment_.empty()) {
                    ic_->commitString(lastSegment_);
                }
                reset();
            }
        } else if (event.key().check(FcitxKey_Return) ||
                   event.key().check(FcitxKey_KP_Enter)) {
            auto commit = pinyinModePrefix_ + pinyinModeBuffer_.userInput();
            if (!commit.empty()) {
                ic_->commitString(commit);
            }
            reset();
        }
    }
    if (!needUpdate) {
        return false;
    }
    auto &inputPanel = ic_->inputPanel();
    ic_->inputPanel().reset();

    if (!pinyinModeBuffer_.empty()) {
        const auto &dict = engine_->pinyinDict();
        const auto &lm = engine_->pinyinModel();
        const auto &config = context_->config();
        auto pinyin = libime::PinyinEncoder::encodeOneUserPinyin(
            pinyinModeBuffer_.userInput());

        auto candidateList = std::make_unique<CommonCandidateList>();
        candidateList->setLayoutHint(*config.candidateLayoutHint);
        candidateList->setCursorPositionAfterPaging(
            CursorPositionAfterPaging::ResetToFirst);
        candidateList->setSelectionKey(*config.selection);
        candidateList->setPageSize(*config.pageSize);

        std::vector<std::pair<std::string, float>> pinyinWords;

        dict.matchWords(pinyin.data(), pinyin.size(),
                        [&pinyinWords, &lm](std::string_view,
                                            std::string_view hanzi, float) {
                            pinyinWords.emplace_back(hanzi,
                                                     lm.singleWordScore(hanzi));
                            return true;
                        });

        std::sort(pinyinWords.begin(), pinyinWords.end(),
                  [](const auto &lhs, const auto &rhs) {
                      return lhs.second > rhs.second;
                  });

        for (auto &p : pinyinWords) {
            candidateList->append<TablePinyinCandidateWord>(
                engine_, std::move(p.first), context_->dict(),
                *config.displayCustomHint);
        }

        if (candidateList->size()) {
            candidateList->setGlobalCursorIndex(0);
            inputPanel.setCandidateList(std::move(candidateList));
        }
    } else {
        if (!lastSegment_.empty()) {
            inputPanel.setAuxDown(Text(lastSegment_));
        }
    }
    Text preeditText;
    preeditText.append(pinyinModePrefix_);
    preeditText.append(pinyinModeBuffer_.userInput());
    if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
        preeditText.setCursor(0);
        inputPanel.setClientPreedit(preeditText);
    } else {
        preeditText.setCursor(preeditText.textLength());
        inputPanel.setPreedit(preeditText);
    }
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    return true;
}

bool TableState::handleForgetWord(KeyEvent &event) {
    auto *inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList || candidateList->size() == 0) {
        return false;
    }
    if (mode_ == TableMode::Normal &&
        event.key().checkKeyList(*engine_->config().forgetWord)) {
        mode_ = TableMode::ForgetWord;
        event.filterAndAccept();
        updateUI(/*keepOldCursor=*/true);
        return true;
    }
    if (mode_ == TableMode::ForgetWord && event.key().check(FcitxKey_Escape)) {
        mode_ = TableMode::Normal;
        event.filterAndAccept();
        updateUI(/*keepOldCursor=*/true);
        return true;
    }

    if (mode_ == TableMode::ForgetWord) {
        event.filterAndAccept();
        return true;
    }
    return false;
}

bool TableState::handleLookupPinyinOrModifyDictionaryMode(KeyEvent &event) {
    // Lookup pinyin and addPhrase may share some code.
    bool needUpdate = false;
    if (mode_ == TableMode::Normal) {
        if (event.key().checkKeyList(*engine_->config().lookupPinyin)) {
            mode_ = TableMode::LookupPinyin;
        } else if (event.key().checkKeyList(
                       *engine_->config().modifyDictionary)) {
            mode_ = TableMode::ModifyDictionary;
        }
        if (mode_ == TableMode::Normal) {
            return false;
        }

        commitBuffer(false);
        lookupPinyinIndex_ = 0;
        lookupPinyinString_.assign(lastCommit_.begin(), lastCommit_.end());
        if (ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) &&
            ic_->surroundingText().isValid()) {
            auto text = ic_->surroundingText().selectedText();
            if (!text.empty()) {
                lookupPinyinString_.clear();
                auto range = utf8::MakeUTF8CharRange(text);
                for (auto iter = std::begin(range), end = std::end(range);
                     iter != end; ++iter) {
                    lookupPinyinString_.emplace_back("", iter.view());
                }
            }
        }
        if (mode_ == TableMode::ModifyDictionary) {
            if (lookupPinyinIndex_ + 1 < lookupPinyinString_.size()) {
                lookupPinyinIndex_ = 1;
            }
        }
        needUpdate = true;
    } else if (mode_ != TableMode::LookupPinyin &&
               mode_ != TableMode::ModifyDictionary &&
               mode_ != TableMode::ForgetWord) {
        return false;
    }

    event.filterAndAccept();
    if (event.key().check(FcitxKey_Left) ||
        event.key().check(FcitxKey_KP_Left)) {
        needUpdate = true;
        if (lookupPinyinString_.size() != 0) {
            lookupPinyinIndex_ += 1;
            if (lookupPinyinIndex_ >= lookupPinyinString_.size()) {
                lookupPinyinIndex_ = lookupPinyinString_.size() - 1;
            }
        }
    } else if (event.key().check(FcitxKey_Right) ||
               event.key().check(FcitxKey_KP_Right)) {
        needUpdate = true;
        if (lookupPinyinString_.size() != 0) {
            if (lookupPinyinIndex_ >= lookupPinyinString_.size()) {
                lookupPinyinIndex_ = lookupPinyinString_.size() - 1;
            } else if (lookupPinyinIndex_ > 0) {
                lookupPinyinIndex_ -= 1;
            }
        }
    }

    auto getSubString = [&]() {
        if (lookupPinyinIndex_ >= lookupPinyinString_.size()) {
            lookupPinyinIndex_ = lookupPinyinString_.size() - 1;
        }
        std::string str;
        std::vector<std::string> hints;
        for (auto idx = lookupPinyinString_.size() - lookupPinyinIndex_ - 1;
             idx < lookupPinyinString_.size(); ++idx) {
            str += lookupPinyinString_[idx].second;
            hints.push_back(lookupPinyinString_[idx].first);
        }
        return std::make_pair(str, hints);
    };

    if (!lookupPinyinString_.empty()) {
        if ((event.key().check(FcitxKey_space) ||
             event.key().check(FcitxKey_Return) ||
             event.key().check(FcitxKey_KP_Space) ||
             event.key().check(FcitxKey_KP_Enter)) &&
            mode_ == TableMode::ModifyDictionary) {
            auto subString = getSubString();
            std::string result;
            if (context_->dict().generateWithHint(subString.first,
                                                  subString.second, result)) {
                auto wordFlag =
                    context_->dict().wordExists(result, subString.first);
                if (wordFlag == libime::PhraseFlag::Invalid) {
                    context_->mutableDict().insert(result, subString.first,
                                                   libime::PhraseFlag::User);
                    reset();
                    return true;
                } else if (wordFlag == libime::PhraseFlag::Auto) {
                    context_->mutableDict().removeWord(result, subString.first);
                    context_->mutableDict().insert(result, subString.first,
                                                   libime::PhraseFlag::User);
                    reset();
                }
            }
        } else if (event.key().checkKeyList(std::initializer_list<Key>{
                       Key(FcitxKey_BackSpace), Key(FcitxKey_Delete),
                       Key(FcitxKey_KP_Delete)}) &&
                   mode_ == TableMode::ModifyDictionary) {
            auto subString = getSubString();
            std::string result;
            if (context_->dict().generateWithHint(subString.first,
                                                  subString.second, result)) {
                auto flag =
                    context_->dict().wordExists(result, subString.first);
                if (flag != libime::PhraseFlag::Invalid) {
                    if ((flag == libime::PhraseFlag::User ||
                         flag == libime::PhraseFlag::None ||
                         flag == libime::PhraseFlag::Auto) &&
                        event.key().check(FcitxKey_Delete)) {
                        context_->mutableDict().removeWord(result,
                                                           subString.first);
                    }
                    context_->mutableModel().history().forget(subString.first);
                    reset();
                    return true;
                }
            }
        }
    }

    if (needUpdate) {
        auto &inputPanel = ic_->inputPanel();
        inputPanel.reset();
        if (lookupPinyinString_.empty()) {
            inputPanel.setAuxUp(Text(
                _("Please use this functionality after typing some text.")));
        } else {
            auto subString = getSubString();
            auto chr = utf8::getChar(subString.first);

            if (mode_ == TableMode::LookupPinyin) {
                Text auxUp(_("Use Left and Right to select character: "));
                auxUp.append(utf8::UCS4ToUTF8(chr));
                inputPanel.setAuxUp(auxUp);
                auto result =
                    engine_->pinyinhelper()->call<IPinyinHelper::lookup>(chr);
                if (!result.empty()) {
                    inputPanel.setAuxDown(Text(stringutils::join(result, " ")));
                } else {
                    inputPanel.setAuxDown(Text(_("Could not find pinyin.")));
                }
            } else {
                Text auxUp(_("Use Left and Right to select text. "));
                Text auxDown;
                if (lookupPinyinIndex_ >= 1) {
                    std::string result;
                    if (context_->dict().generateWithHint(
                            subString.first, subString.second, result)) {
                        auxDown.append(
                            fmt::format(_("{0}: {1}"), subString.first,
                                        context_->customHint(result)));
                        auto flag = context_->dict().wordExists(
                            result, subString.first);
                        if (flag == libime::PhraseFlag::Invalid ||
                            flag == libime::PhraseFlag::Auto) {
                            auxUp.append(_("Press space or enter to insert."));
                        }
                        if (flag != libime::PhraseFlag::Invalid) {
                            auxUp.append(_("Press Backspace to forget."));
                        }
                        if (flag == libime::PhraseFlag::User) {
                            auxUp.append(_("Press Delete to remove."));
                        }
                    } else {
                        auxDown.append(fmt::format(
                            _("{0}: No corresponding code."), subString.first));
                    }
                    auxDown.append(" ");
                }
                auto chrString = utf8::UCS4ToUTF8(chr);
                auto chrCode = context_->dict().reverseLookup(chrString);
                if (!chrCode.empty()) {
                    auxDown.append(
                        fmt::format(_("{0}: {1}"), chrString, chrCode));
                } else {
                    auxDown.append(
                        fmt::format(_("{0} is not in table."), chrString));
                }
                inputPanel.setAuxUp(auxUp);
                inputPanel.setAuxDown(auxDown);
            }
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    return true;
}

void TableState::forgetCandidateWord(size_t idx) {
    mode_ = TableMode::Normal;
    auto oldCode = context_->currentCode();
    auto code = TableContext::code(context_->candidates()[idx]);
    if (!code.empty()) {
        auto word = context_->candidates()[idx].toString();
        commitBuffer(false);
        context_->mutableDict().removeWord(code, word);
        context_->mutableModel().history().forget(word);
    } else {
        return;
    }
    context_->clear();
    {
        CommitAfterSelectWrapper commitAfterSelectRAII(this);
        context_->type(oldCode);
    }

    updateUI(/*keepOldCursor=*/true);
}

void TableState::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    bool needUpdate = false;
    auto *inputContext = event.inputContext();
    bool lastIsPunc = lastIsPunc_;
    auto *context = updateContext(&entry);
    if (!context) {
        return;
    }

    const auto &config = context->config();
    // 2nd/3rd selection is allowed to be modifier only, handle them before we
    // skip the release.
    if (handle2nd3rdCandidate(config, event)) {
        return;
    }

    // by pass all key release and by pass all modifier
    if (event.isRelease() || event.key().isModifier()) {
        return;
    }

    lastIsPunc_ = false;

    if (handleCandidateList(config, event)) {
        return;
    }

    // We have special handling of escape here.
    if (handleForgetWord(event)) {
        return;
    }

    if (event.key().check(FcitxKey_Escape)) {
        if (mode_ != TableMode::Normal) {
            event.filterAndAccept();
        }
        if (!isContextEmpty()) {
            event.filterAndAccept();
        }
        reset();
        if (event.accepted()) {
            return;
        }
    }

    if (handlePinyinMode(event)) {
        return;
    }

    if (handleLookupPinyinOrModifyDictionaryMode(event)) {
        return;
    }

    auto chr = Key::keySymToUnicode(event.key().sym());
    auto str = utf8::UCS4ToUTF8(chr);

    if (engine_->quickphrase() && !event.key().hasModifier() &&
        !config.quickphraseText->empty() && !str.empty() &&
        config.quickphraseText->find(str) != std::string::npos) {
        std::string text = context_->currentCode();
        commitBuffer(false);
        text.append(str);
        reset();
        engine_->quickphrase()->call<IQuickPhrase::trigger>(inputContext, "",
                                                            "", "", "", Key());
        engine_->quickphrase()->call<IQuickPhrase::setBuffer>(inputContext,
                                                              text);
        event.filterAndAccept();
        return;
    }
    if (!event.key().hasModifier() && chr && context->isValidInput(chr)) {

        {
            CommitAfterSelectWrapper commitAfterSelectRAII(this);
            context->type(str);
        }
        if (context->candidates().empty() &&
            ((*config.commitAfterSelect && context->currentCode() == str) ||
             (!*config.commitAfterSelect && context->userInput() == str))) {
            // This means it is not a valid start, make it go through the punc.
            context->backspace();
        } else {
            event.filterAndAccept();
        }
    } else if (!isContextEmpty()) {
        if (event.key().check(FcitxKey_Return, KeyState::Shift) ||
            event.key().check(FcitxKey_KP_Enter, KeyState::Shift)) {
            if (*config.commitAfterSelect) {
                commitBuffer(true);
            } else {
                inputContext->commitString(context->userInput());
                context->clear();
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Tab) ||
                   event.key().check(FcitxKey_KP_Tab)) {
            {
                CommitAfterSelectWrapper commitAfterSelectRAII(this);
                autoSelectCandidate();
            }
            if (context->selected()) {
                commitBuffer(false);
            }
            event.filterAndAccept();
        } else if (event.key().sym() == FcitxKey_Return ||
                   event.key().sym() == FcitxKey_KP_Enter) {
            if (*config.commitAfterSelect) {
                if (!context->selected()) {
                    event.filterAndAccept();
                }
                commitBuffer(true);
            } else {
                inputContext->commitString(context->userInput());
                context->clear();
                event.filterAndAccept();
            }
        } else if (event.key().check(FcitxKey_BackSpace) ||
                   event.key().check(FcitxKey_BackSpace, KeyState::Ctrl)) {
            // Commit the last segment if it is selected.
            if (*config.commitAfterSelect && context->selected() &&
                (std::get<bool>(
                     context->selectedSegment(context->selectedSize() - 1)) ||
                 *config.commitInvalidSegment)) {
                commitBuffer(false);
                updateUI();
                return;
            }
            if (event.key().check(FcitxKey_BackSpace, KeyState::Ctrl)) {
                // For non-commitAfterSelect, remove the whole segment, or the
                // current code.
                if (!*config.commitAfterSelect && context->selected()) {
                    auto cursor = context->cursor();
                    context_->erase(cursor - context->selectedSegmentLength(
                                                 context->selectedSize() - 1),
                                    cursor);
                } else {
                    auto cursor = context->cursor();
                    context_->erase(
                        cursor - utf8::length(context_->currentCode()), cursor);
                }
            } else {
                context->backspace();
            }
            event.filterAndAccept();
        } else if (event.key().isCursorMove() ||
                   event.key().check(FcitxKey_Delete)) {
            // if it gonna commit something
            commitBuffer(true);
            needUpdate = true;
        } else if (!context->selected()) {
            // key to handle when it is not empty.
            if (event.key().check(FcitxKey_space)) {
                autoSelectCandidate();
                return event.filterAndAccept();
            }
        }
    } else if (event.key().check(FcitxKey_BackSpace) && lastIsPunc) {
        auto puncStr = engine_->punctuation()->call<IPunctuation::cancelLast>(
            entry.languageCode(), inputContext);
        if (!puncStr.empty()) {
            // forward the original key is the best choice.
            auto ref = inputContext->watch();
            cancelLastEvent_ = engine_->instance()->eventLoop().addTimeEvent(
                CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 300, 0,
                [this, ref, puncStr](EventSourceTime *, uint64_t) {
                    if (auto *inputContext = ref.get()) {
                        inputContext->commitString(puncStr);
                    }
                    cancelLastEvent_.reset();
                    return true;
                });
            event.filter();
            return;
        }
    }

    do {
        if (event.filtered()) {
            break;
        }
        if (*context->config().commitAfterSelect && isContextEmpty()) {
            if (event.key().check(FcitxKey_Delete) ||
                event.key().check(FcitxKey_BackSpace) ||
                event.key().check(FcitxKey_Delete, KeyState::Ctrl) ||
                event.key().check(FcitxKey_BackSpace, KeyState::Ctrl) ||
                event.key().isCursorMove() ||
                event.key().sym() == FcitxKey_Return ||
                event.key().sym() == FcitxKey_KP_Enter) {
                commitBuffer(false);
            }
        }

        if (event.key().hasModifier() || !chr) {
            // Handle quick phrase with modifier
            if (event.key().check(*config.quickphrase) &&
                engine_->quickphrase()) {
                engine_->quickphrase()->call<IQuickPhrase::trigger>(
                    inputContext, "", "", "", "", Key());
                event.filterAndAccept();
                return;
            }
            break;
        }
        // if current key will produce some string, select the candidate.
        if (!autoSelectCandidate()) {
            commitBuffer(true);
        }
        std::string punc, puncAfter;
        if (!*context->config().ignorePunc && !event.key().isKeyPad()) {
            std::tie(punc, puncAfter) =
                engine_->punctuation()->call<IPunctuation::pushPunctuationV2>(
                    entry.languageCode(), inputContext, chr);
        }
        if (event.key().check(*config.quickphrase) && engine_->quickphrase()) {
            auto s = !punc.empty() ? punc + puncAfter : utf8::UCS4ToUTF8(chr);
            auto alt = !punc.empty() ? utf8::UCS4ToUTF8(chr) : "";
            std::string text;
            if (!s.empty()) {
                text += alt + _(" for ") + s;
            }
            if (!alt.empty()) {
                text += _(" Return for ") + alt;
            }
            engine_->quickphrase()->call<IQuickPhrase::trigger>(
                inputContext, text, "", s, alt, *config.quickphrase);
            event.filterAndAccept();
            return;
        }

        if (!punc.empty()) {
            event.filterAndAccept();
            inputContext->commitString(punc + puncAfter);
            if (size_t length = utf8::lengthValidated(puncAfter);
                length != 0 && length != utf8::INVALID_LENGTH) {
                for (size_t i = 0; i < length; i++) {
                    inputContext->forwardKey(Key(FcitxKey_Left));
                }
            }
        }
        lastIsPunc_ = true;
    } while (0);

    if ((event.filtered() && event.accepted()) || needUpdate) {
        updateUI();
    }
}

bool TableState::handle2nd3rdCandidate(const TableConfig &config,
                                       KeyEvent &event) {
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
        {*config.secondCandidate, 1},
        {*config.thirdCandidate, 2},
    };

    int keyReleased = keyReleased_;
    int keyReleasedIndex = keyReleasedIndex_;
    // Keep these two values, and reset them in the state
    keyReleased_ = -1;
    keyReleasedIndex_ = -2;
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
                keyReleased_ = idx;
                keyReleasedIndex_ = keyIdx;
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

void TableState::commitBuffer(bool commitCode, bool noRealCommit) {
    auto *context = context_.get();
    if (!context) {
        return;
    }

    if (mode_ == TableMode::Pinyin && !noRealCommit) {
        auto commit = pinyinModePrefix_ + pinyinModeBuffer_.userInput();
        if (!commit.empty()) {
            ic_->commitString(commit);
        }
        reset();
        return;
    }

    std::string sentence;
    if (!*context->config().commitAfterSelect) {
        sentence = commitSegements(0, context->selectedSize());
    }

    if (commitCode) {
        sentence += context->currentCode();
    }
    TABLE_DEBUG() << "TableState::commitBuffer " << sentence << " "
                  << context->selectedSize();

    if (!noRealCommit && !sentence.empty()) {
        ic_->commitString(sentence);
    }
    // If commitAfterSelect && !useContextBasedOrder, learnLast will be used.
    if (!ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive) &&
        !(*context->config().commitAfterSelect &&
          !*context->config().useContextBasedOrder)) {
        context->learn();
    }
    context->clear();
}

void TableState::commitAfterSelect(int commitFrom) {
    auto *context = context_.get();
    if (!context) {
        return;
    }

    const auto &config = context->config();
    if (!*config.commitAfterSelect) {
        return;
    }
    std::string sentence =
        commitSegements(commitFrom, context_->selectedSize());
    if (sentence.empty()) {
        return;
    }
    ic_->commitString(sentence);
    if (!*config.useContextBasedOrder) {
        if (!ic_->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive)) {
            context->learnLast();
        }
    }
}

void TableState::updateUI(bool keepOldCursor) {

    int cursor = 0;
    if (keepOldCursor) {
        if (auto candidateList = ic_->inputPanel().candidateList()) {
            if (auto commonCandidateList =
                    dynamic_cast<CommonCandidateList *>(candidateList.get())) {
                cursor = commonCandidateList->globalCursorIndex();
            }
        }
    }
    if (cursor < 0) {
        cursor = 0;
    }

    ic_->inputPanel().reset();

    auto *context = context_.get();
    if (!context) {
        return;
    }
    const auto &config = context->config();
    if (!context->userInput().empty()) {
        auto candidates = context->candidates();
        auto &inputPanel = ic_->inputPanel();
        if (!candidates.empty()) {
            auto candidateList = std::make_unique<CommonCandidateList>();
            size_t idx = 0;
            candidateList->setLayoutHint(*config.candidateLayoutHint);
            candidateList->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);
            candidateList->setSelectionKey(*config.selection);
            candidateList->setPageSize(*config.pageSize);

            for (const auto &candidate : candidates) {
                auto candidateString = candidate.toString();
                Text text;
                text.append(candidateString);
                std::string hint;
                if (*config.hint) {
                    hint =
                        context->candidateHint(idx, *config.displayCustomHint);
                }
                if (!hint.empty()) {
                    text.append(*config.hintSeparator);
                    text.append(hint);
                }
                if (!config.markerForAutoPhrase->empty() &&
                    TableContext::isAuto(candidate.sentence())) {
                    text.append(*config.markerForAutoPhrase);
                }
                candidateList->append<TableCandidateWord>(engine_,
                                                          std::move(text), idx);
                idx++;
            }
            if (candidateList->size()) {
                auto page = cursor / *config.pageSize;
                if (page >= candidateList->totalPages()) {
                    page = candidateList->totalPages() - 1;
                }
                candidateList->setPage(page);
                if (cursor >= candidateList->totalSize()) {
                    cursor = candidateList->totalSize() - 1;
                }
                candidateList->setGlobalCursorIndex(cursor);
            }
            inputPanel.setCandidateList(std::move(candidateList));
        }
        const bool useClientPreedit =
            ic_->capabilityFlags().test(CapabilityFlag::Preedit);
        if (*config.displayCustomHint && context->dict().hasCustomPrompt()) {
            if (useClientPreedit) {
                inputPanel.setClientPreedit(context->preeditText(
                    /*hint=*/false, /*clientPreedit=*/true));
            }
            inputPanel.setPreedit(
                context->preeditText(/*hint=*/true, /*clientPreedit=*/false));
        } else {
            Text preeditText =
                context->preeditText(/*hint=*/false, useClientPreedit);
            if (useClientPreedit) {
                inputPanel.setClientPreedit(preeditText);
            } else {
                inputPanel.setPreedit(preeditText);
            }
        }
        if (mode_ == TableMode::ForgetWord) {
            inputPanel.setPreedit(Text());
            inputPanel.setAuxUp(
                Text(_("Select candidate to be removed from history:")));
        }
    }
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}
} // namespace fcitx
