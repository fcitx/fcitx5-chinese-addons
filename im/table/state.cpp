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
        if (auto *context = state->context(nullptr)) {
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
        auto state = inputContext->propertyFor(&engine_->factory());
        // nullptr means use the last requested entry.
        auto *context = state->context(nullptr);
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
        : CandidateWord(), engine_(engine), word_(std::move(word)) {
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
        auto state = inputContext->propertyFor(&engine_->factory());
        inputContext->commitString(word_);
        state->pushLastCommit(word_);
        state->reset();
    }

    TableEngine *engine_;
    std::string word_;
};
} // namespace

TableContext *TableState::context(const InputMethodEntry *entry) {
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

void TableState::pushLastCommit(const std::string &lastSegment) {
    if (lastSegment.empty() ||
        ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) {
        return;
    }

    lastCommit_ += lastSegment;
    constexpr size_t limit = 10;
    auto length = utf8::length(lastCommit_);
    TABLE_DEBUG() << "TableState::pushLastCommit " << lastSegment
                  << " length: " << utf8::length(lastSegment);
    if (utf8::length(lastSegment) == 1) {
        lastSingleCharCommit_.push_back(lastSegment);
        while (lastSingleCharCommit_.size() > 10) {
            lastSingleCharCommit_.pop_front();
        }
        auto singleCharString = stringutils::join(lastSingleCharCommit_, "");
        TABLE_DEBUG() << "learnAutoPhrase " << singleCharString;
        context_->learnAutoPhrase(singleCharString);
    } else {
        lastSingleCharCommit_.clear();
    }

    if (length > limit) {
        auto iter = lastCommit_.begin();
        while (length > limit) {
            iter = utf8::nextChar(iter);
            length--;
        }
        lastCommit_ =
            lastCommit_.substr(std::distance(lastCommit_.begin(), iter));
    }
    lastSegment_ = lastSegment;
}

void TableState::reset(const InputMethodEntry *entry) {
    auto context = this->context(entry);
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

bool TableState::handleCandidateList(const TableConfig &config,
                                     KeyEvent &event) {
    auto inputContext = event.inputContext();
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
        auto pageable = candidateList->toPageable();
        if (!pageable->hasPrev()) {
            if (pageable->usedNextBefore()) {
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
    if (auto movable = candidateList->toCursorMovable()) {
        if (event.key().checkKeyList(*config.nextCandidate)) {
            movable->nextCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        } else if (event.key().checkKeyList(*config.prevCandidate)) {
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
    auto context = context_.get();
    const auto &pinyinKey = *context->config().pinyinKey;
    if (pinyinKey.sym() == FcitxKey_None) {
        return false;
    }
    bool needUpdate = false;
    if (mode_ == TableMode::Normal && event.key().check(pinyinKey)) {
        auto chr = Key::keySymToUnicode(event.key().sym());
        if (*context->config().commitAfterSelect) {
            if (!context->empty() && !context->selected()) {
                if (context->isValidInput(chr)) {
                    return false;
                }
            }
        } else {
            if (context->size() != 0) {
                if (context->isValidInput(chr)) {
                    return false;
                }
            }
        }
        commitBuffer(false);
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
            if (pinyinModeBuffer_.size()) {
                pinyinModeBuffer_.backspace();
                needUpdate = true;
            } else {
                reset();
                return true;
            }
        } else if (event.key().check(FcitxKey_space)) {
            auto candidateList = ic_->inputPanel().candidateList();
            if (candidateList && candidateList->size()) {
                int idx = candidateList->cursorIndex();
                if (idx < 0) {
                    idx = 0;
                }
                candidateList->candidate(idx).select(ic_);
                return true;
            } else if (!pinyinModeBuffer_.size()) {
                if (!lastSegment_.empty()) {
                    ic_->commitString(lastSegment_);
                }
                reset();
            }
        } else if (event.key().check(FcitxKey_Return)) {
            auto commit = pinyinModePrefix_ + pinyinModeBuffer_.userInput();
            if (!commit.empty()) {
                ic_->commitString(commit);
            }
            reset();
        }
    }
    if (needUpdate) {
        auto &inputPanel = ic_->inputPanel();
        ic_->inputPanel().reset();

        if (pinyinModeBuffer_.size()) {
            auto &dict = engine_->pinyinDict();
            auto &lm = engine_->pinyinModel();
            auto pinyin = libime::PinyinEncoder::encodeOneUserPinyin(
                pinyinModeBuffer_.userInput());

            auto candidateList = std::make_unique<CommonCandidateList>();
            candidateList->setSelectionKey(*context_->config().selection);
            candidateList->setPageSize(*context_->config().pageSize);
            std::vector<std::pair<std::string, float>> pinyinWords;

            dict.matchWords(pinyin.data(), pinyin.size(),
                            [&pinyinWords, &lm](std::string_view,
                                                std::string_view hanzi, float) {
                                pinyinWords.emplace_back(
                                    hanzi, lm.singleWordScore(hanzi));
                                return true;
                            });

            std::sort(pinyinWords.begin(), pinyinWords.end(),
                      [](const auto &lhs, const auto &rhs) {
                          return lhs.second > rhs.second;
                      });

            for (auto &p : pinyinWords) {
                candidateList->append<TablePinyinCandidateWord>(
                    engine_, std::move(p.first), context_->dict(),
                    *context_->config().displayCustomHint);
            }

            if (candidateList->size()) {
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
    }
    return true;
}

bool TableState::handleForgetWord(KeyEvent &event) {
    auto inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList || candidateList->size() == 0) {
        return false;
    }
    if (mode_ == TableMode::Normal &&
        event.key().checkKeyList(*engine_->config().forgetWord)) {
        mode_ = TableMode::ForgetWord;
        event.filterAndAccept();
        updateUI();
        return true;
    }
    if (mode_ == TableMode::ForgetWord && event.key().check(FcitxKey_Escape)) {
        mode_ = TableMode::Normal;
        event.filterAndAccept();
        updateUI();
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
    auto context = context_.get();
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

        if (context->size() != 0) {
            commitBuffer(false);
        }
        lookupPinyinIndex_ = 0;
        lookupPinyinString_ = lastCommit_;
        if (ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) &&
            ic_->surroundingText().isValid()) {
            auto text = ic_->surroundingText().selectedText();
            if (!text.empty()) {
                lookupPinyinString_ = std::move(text);
            }
        }
        needUpdate = true;
    } else if (mode_ != TableMode::LookupPinyin &&
               mode_ != TableMode::ModifyDictionary &&
               mode_ != TableMode::ForgetWord) {
        return false;
    }

    event.filterAndAccept();
    if (event.key().check(FcitxKey_Left)) {
        needUpdate = true;
        auto length = utf8::length(lookupPinyinString_);
        if (length != 0) {
            lookupPinyinIndex_ += 1;
            if (lookupPinyinIndex_ >= length) {
                lookupPinyinIndex_ = length - 1;
            }
        }
    } else if (event.key().check(FcitxKey_Right)) {
        needUpdate = true;
        auto length = utf8::length(lookupPinyinString_);
        if (length != 0) {
            if (lookupPinyinIndex_ >= length) {
                lookupPinyinIndex_ = length - 1;
            } else if (lookupPinyinIndex_ > 0) {
                lookupPinyinIndex_ -= 1;
            }
        }
    }

    auto length = utf8::length(lookupPinyinString_);
    auto getSubString = [&]() {
        if (lookupPinyinIndex_ >= length) {
            lookupPinyinIndex_ = length - 1;
        }
        auto idx = length - lookupPinyinIndex_ - 1;
        auto iter = utf8::nextNChar(lookupPinyinString_.begin(), idx);
        return std::string(iter, lookupPinyinString_.end());
    };

    if (length != 0 && length != utf8::INVALID_LENGTH) {
        if (event.key().check(FcitxKey_space) &&
            mode_ == TableMode::ModifyDictionary) {
            auto subString = getSubString();
            std::string result;
            if (context_->dict().generate(subString, result)) {
                if (context_->dict().wordExists(result, subString) ==
                    libime::PhraseFlag::Invalid) {
                    context_->mutableDict().insert(subString);
                    reset();
                    return true;
                }
            }
        } else if (event.key().checkKeyList(std::initializer_list<Key>{
                       Key(FcitxKey_BackSpace), Key(FcitxKey_Delete)}) &&
                   mode_ == TableMode::ModifyDictionary) {
            auto subString = getSubString();
            std::string result;
            if (context_->dict().generate(subString, result)) {
                auto flag = context_->dict().wordExists(result, subString);
                if (flag != libime::PhraseFlag::Invalid) {
                    if (flag == libime::PhraseFlag::User &&
                        event.key().check(FcitxKey_Delete)) {
                        context_->mutableDict().removeWord(result, subString);
                    }
                    context_->mutableModel().history().forget(subString);
                    reset();
                    return true;
                }
            }
        }
    }

    if (needUpdate) {
        auto &inputPanel = ic_->inputPanel();
        inputPanel.reset();
        if (length == 0 || length == utf8::INVALID_LENGTH) {
            inputPanel.setAuxUp(Text(
                _("Please use this functionality after typing some text.")));
        } else {
            auto subString = getSubString();
            auto chr = utf8::getChar(subString);

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
                    if (context_->dict().generate(subString, result)) {
                        auxDown.append(
                            fmt::format(_("{0}: {1}"), subString,
                                        context_->customHint(result)));
                        auto flag =
                            context_->dict().wordExists(result, subString);
                        if (flag == libime::PhraseFlag::Invalid) {
                            auxUp.append(_("Press space to insert."));
                        }
                        if (flag != libime::PhraseFlag::Invalid) {
                            auxUp.append(_("Press Backspace to forget."));
                        }
                        if (flag == libime::PhraseFlag::User) {
                            auxUp.append(_("Press Delete to remove."));
                        }
                    } else {
                        auxDown.append(fmt::format(
                            _("{0}: No corresponding code."), subString));
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
    auto code = TableContext::code(context_->candidates()[idx]);
    if (!code.empty()) {
        auto word = context_->candidates()[idx].toString();
        context_->mutableDict().removeWord(code, word);
        context_->mutableModel().history().forget(word);
    }
    context_->clear();
    mode_ = TableMode::Normal;

    updateUI();
}

void TableState::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    bool needUpdate = false;
    auto inputContext = event.inputContext();
    bool lastIsPunc = lastIsPunc_;
    auto context = this->context(&entry);
    if (!context) {
        return;
    }

    auto &config = context->config();
    // 2nd/3rd selection is allowed to be modifier only, handle them before we
    // skip the release.
    if (handle2nd3rdCandidate(config, event)) {
        return;
    }

    // by pass all key release and by pass all modifier
    if (event.isRelease() || event.key().isModifier()) {
        return;
    }

    if ((mode_ != TableMode::Normal || context_->size()) &&
        event.key().check(FcitxKey_Escape)) {
        reset();
        return event.filterAndAccept();
    }

    lastIsPunc_ = false;

    if (handleCandidateList(config, event)) {
        return;
    }

    if (handlePinyinMode(event)) {
        return;
    }

    if (handleForgetWord(event)) {
    }

    if (handleLookupPinyinOrModifyDictionaryMode(event)) {
        return;
    }

    auto chr = Key::keySymToUnicode(event.key().sym());
    if (!event.key().hasModifier() && chr && context->isValidInput(chr)) {
        auto str = utf8::UCS4ToUTF8(chr);
        {
            CommitAfterSelectWrapper commitAfterSelectRAII(this);
            context->type(str);
        }
        if (context->candidates().empty() && context->currentCode() == str) {
            // This means it is not a valid start, make it go through the punc.
            context->backspace();
        } else {
            event.filterAndAccept();
        }
    } else if (context->size()) {
        if (event.key().check(FcitxKey_Return, KeyState::Shift)) {
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
        } else if (event.key().check(FcitxKey_Tab)) {
            {
                CommitAfterSelectWrapper commitAfterSelectRAII(this);
                context->autoSelect();
            }
            if (context->selected()) {
                commitBuffer(false);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Return) && !context->empty()) {
            if (!*config.commitAfterSelect || !context->selected()) {
                event.filterAndAccept();
            }
            commitBuffer(true);
        } else if (event.key().check(FcitxKey_BackSpace)) {
            // Commit the last segement if it is selected.
            if (context->selected() && (std::get<bool>(context->selectedSegment(
                                            context->selectedSize() - 1)) ||
                                        *config.commitInvalidSegment)) {
                commitBuffer(false);
                needUpdate = true;
                event.filter();
            } else {
                context->backspace();
                event.filterAndAccept();
            }
        } else if (event.key().isCursorMove() ||
                   event.key().check(FcitxKey_Delete)) {
            // if it gonna commit something
            commitBuffer(true);
            needUpdate = true;
            event.filter();
        } else if (!context->selected()) {
            // key to handle when it is not empty.
            if (event.key().check(FcitxKey_space)) {
                auto candidateList = ic_->inputPanel().candidateList();
                if (candidateList && candidateList->size()) {
                    int idx = candidateList->cursorIndex();
                    if (idx < 0) {
                        idx = 0;
                    }
                    candidateList->candidate(idx).select(ic_);
                    return event.filterAndAccept();
                }
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
                    if (auto inputContext = ref.get()) {
                        inputContext->commitString(puncStr);
                    }
                    cancelLastEvent_.reset();
                    return true;
                });
            event.filter();
            return;
        }
    }
    if (!event.filtered()) {
        if (event.key().hasModifier() || !chr) {
            return;
        }
        // if current key will produce some string, do the auto select.
        {
            CommitAfterSelectWrapper commitAfterSelectRAII(this);
            context->autoSelect();
            needUpdate = true;
        }
        if (context->selected()) {
            commitBuffer(false);
        }
        std::string punc;
        if (!*context->config().ignorePunc || event.key().isKeyPad()) {
            punc = engine_->punctuation()->call<IPunctuation::pushPunctuation>(
                entry.languageCode(), inputContext, chr);
        }
        if (event.key().check(*config.quickphrase) && engine_->quickphrase()) {
            auto s = punc.size() ? punc : utf8::UCS4ToUTF8(chr);
            auto alt = punc.size() ? utf8::UCS4ToUTF8(chr) : "";
            std::string text;
            if (s.size()) {
                text += alt + _(" for ") + s;
            }
            if (alt.size()) {
                text += _(" Return for ") + alt;
            }
            engine_->quickphrase()->call<IQuickPhrase::trigger>(
                inputContext, text, "", s, alt, Key(FcitxKey_semicolon));
            event.filterAndAccept();
            return;
        }

        if (punc.size()) {
            event.filterAndAccept();
            inputContext->commitString(punc);
        }
        lastIsPunc_ = true;
    }

    if ((event.filtered() && event.accepted()) || needUpdate) {
        updateUI();
    }
    if (inputContext->capabilityFlags().test(
            CapabilityFlag::KeyEventOrderFix) &&
        !event.filtered()) {
        // Re-forward the event to ensure we got delivered later than
        // commit.
        event.filterAndAccept();
        inputContext->forwardKey(event.rawKey(), event.isRelease(),
                                 event.time());
    }
}

bool TableState::handle2nd3rdCandidate(const TableConfig &config,
                                       KeyEvent &event) {
    auto inputContext = event.inputContext();
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
    const bool isModifier = event.origKey().isModifier();
    if (event.isRelease()) {
        int idx = 0;
        for (auto &keyHandler : keyHandlers) {
            if (keyReleased == idx &&
                keyReleasedIndex ==
                    event.origKey().keyListIndex(keyHandler.list)) {
                if (isModifier) {
                    if (keyHandler.selection < candidateList->size()) {
                        candidateList->candidate(keyHandler.selection)
                            .select(inputContext);
                    }
                    event.filterAndAccept();
                    return true;
                } else {
                    event.filter();
                    return true;
                }
            }
            idx++;
        }
    }

    if (!event.filtered() && !event.isRelease()) {
        int idx = 0;
        for (auto &keyHandler : keyHandlers) {
            auto keyIdx = event.origKey().keyListIndex(keyHandler.list);
            if (keyIdx >= 0) {
                keyReleased_ = idx;
                keyReleasedIndex_ = keyIdx;
                if (isModifier) {
                    // don't forward to input method, but make it pass
                    // through to client.
                    event.filter();
                    return true;
                } else {
                    if (keyHandler.selection < candidateList->size()) {
                        candidateList->candidate(keyHandler.selection)
                            .select(inputContext);
                    }
                    event.filterAndAccept();
                    return true;
                }
            }
            idx++;
        }
    }
    return false;
}

void TableState::commitBuffer(bool commitCode, bool noRealCommit) {
    auto context = context_.get();
    if (!context) {
        return;
    }
    std::string sentence;
    if (!*context->config().commitAfterSelect) {
        for (size_t i = 0; i < context->selectedSize(); i++) {
            auto seg = context->selectedSegment(i);
            if (std::get<bool>(seg) ||
                *context->config().commitInvalidSegment) {
                pushLastCommit(std::get<std::string>(seg));
                sentence += std::get<std::string>(seg);
            }
        }
    }

    if (commitCode) {
        sentence += context->currentCode();
    }
    TABLE_DEBUG() << "TableState::commitBuffer " << sentence << " "
                  << context->selectedSize();

    if (!noRealCommit && !sentence.empty()) {
        ic_->commitString(sentence);
    }
    if (!ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) {
        context->learn();
    }
    context->clear();
}

void TableState::commitAfterSelect(int commitFrom) {
    auto context = context_.get();
    if (!context) {
        return;
    }

    auto &config = context->config();
    if (!*config.commitAfterSelect) {
        return;
    }
    std::string sentence;
    for (int i = commitFrom, e = context_->selectedSize(); i < e; i++) {
        auto seg = context->selectedSegment(i);
        if (std::get<bool>(seg) || *config.commitInvalidSegment) {
            pushLastCommit(std::get<std::string>(seg));
            sentence += std::get<std::string>(seg);
        }
    }
    if (!sentence.empty()) {
        ic_->commitString(sentence);
        if (!*config.useContextBasedOrder) {
            if (!ic_->capabilityFlags().testAny(
                    CapabilityFlag::PasswordOrSensitive)) {
                context->learnLast();
            }
        }
    }
}

void TableState::updateUI() {
    ic_->inputPanel().reset();

    auto context = context_.get();
    if (!context) {
        return;
    }
    auto &config = context->config();
    if (context->userInput().size()) {
        auto candidates = context->candidates();
        auto &inputPanel = ic_->inputPanel();
        if (context->candidates().size()) {
            auto candidateList = std::make_unique<CommonCandidateList>();
            size_t idx = 0;
            candidateList->setLayoutHint(*config.candidateLayoutHint);
            candidateList->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);

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
                    text.append(" ~ ");
                    text.append(hint);
                }
                candidateList->append<TableCandidateWord>(engine_,
                                                          std::move(text), idx);
                idx++;
            }
            candidateList->setSelectionKey(*config.selection);
            candidateList->setPageSize(*config.pageSize);
            if (candidateList->size()) {
                candidateList->setGlobalCursorIndex(0);
            }
            inputPanel.setCandidateList(std::move(candidateList));
        }
        if (*config.displayCustomHint && context->dict().hasCustomPrompt()) {
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
                inputPanel.setClientPreedit(context->preeditText(false));
            }
            inputPanel.setPreedit(context->preeditText(true));
        } else {
            Text preeditText = context->preeditText(false);
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
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
