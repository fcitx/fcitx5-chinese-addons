/*
* Copyright (C) 2017~2017 by CSSlayer
* wengxt@gmail.com
*
* This library is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 2.1 of the
* License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; see the file COPYING. If not,
* see <http://www.gnu.org/licenses/>.
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
        context->select(idx_);
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
}

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
    if (lastSegment.empty()) {
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
    lastIsPunc_ = false;
    mode_ = TableMode::Normal;
    pinyinModePrefix_.clear();
    pinyinModeBuffer_.clear();
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
            candidateList->candidate(idx)->select(inputContext);
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
        if (context->size() != 0) {
            commitBuffer(false);
        }
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
                candidateList->candidate(idx)->select(ic_);
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

            dict.matchWords(
                pinyin.data(), pinyin.size(),
                [this, &pinyinWords, &lm](boost::string_view,
                                          boost::string_view hanzi, float) {
                    pinyinWords.emplace_back(hanzi.to_string(),
                                             lm.singleWordScore(hanzi));
                    return true;
                });

            std::sort(pinyinWords.begin(), pinyinWords.end(),
                      [](const auto &lhs, const auto &rhs) {
                          return lhs.second > rhs.second;
                      });

            for (auto &p : pinyinWords) {
                candidateList->append(new TablePinyinCandidateWord(
                    engine_, std::move(p.first), context_->dict(),
                    *context_->config().displayCustomHint));
            }

            if (candidateList->size()) {
                inputPanel.setCandidateList(candidateList.release());
            }
        } else {
            if (!lastSegment_.empty()) {
                inputPanel.setAuxDown(Text(lastSegment_));
            }
        }
        Text preeditText;
        preeditText.append(pinyinModePrefix_);
        preeditText.append(pinyinModeBuffer_.userInput());
        preeditText.setCursor(preeditText.textLength());
        if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
            inputPanel.setClientPreedit(preeditText);
        } else {
            inputPanel.setPreedit(preeditText);
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    return true;
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
               mode_ != TableMode::ModifyDictionary) {
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

void TableState::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    bool needUpdate = false;
    auto inputContext = event.inputContext();
    bool lastIsPunc = lastIsPunc_;
    auto context = this->context(&entry);
    if (!context) {
        return;
    }

    if ((mode_ != TableMode::Normal || context_->size()) &&
        event.key().check(FcitxKey_Escape)) {
        reset();
        return event.filterAndAccept();
    }

    lastIsPunc_ = false;
    auto &config = context->config();

    if (handleCandidateList(config, event)) {
        return;
    }

    if (handlePinyinMode(event)) {
        return;
    }

    if (handleLookupPinyinOrModifyDictionaryMode(event)) {
        return;
    }

    auto chr = Key::keySymToUnicode(event.key().sym());
    if (!event.key().hasModifier() && context->isValidInput(chr)) {
        context->type(utf8::UCS4ToUTF8(chr));
        event.filterAndAccept();
    } else if (context->size()) {
        if (event.key().check(FcitxKey_Return, KeyState::Shift)) {
            inputContext->commitString(context->userInput());
            context->clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Tab)) {
            // if it gonna commit something
            context->autoSelect();
            if (context->selected()) {
                commitBuffer(false);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Return) && !context->selected()) {
            commitBuffer(true);
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_BackSpace)) {
            // Discard the last segement if it is selected.
            if (context->selected()) {
                auto length =
                    context->selectedSegmentLength(context->selectedSize() - 1);
                context->erase(context->size() - length, context->size());
            } else {
                context->backspace();
            }
            event.filterAndAccept();
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
                    candidateList->candidate(idx)->select(ic_);
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
            cancelLastEvent_.reset(engine_->instance()->eventLoop().addTimeEvent(
                CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 300, 0,
                [this, ref, puncStr](EventSourceTime *, uint64_t) {
                    if (auto inputContext = ref.get()) {
                        inputContext->commitString(puncStr);
                    }
                    cancelLastEvent_.reset();
                    return true;
                }));
            event.filter();
            return;
        }
    }
    if (!event.filtered()) {
        if (event.key().hasModifier() || !chr) {
            return;
        }
        // if it gonna commit something
        context->autoSelect();
        if (context->selected()) {
            commitBuffer(false);
            needUpdate = true;
        }
        auto punc = engine_->punctuation()->call<IPunctuation::pushPunctuation>(
            entry.languageCode(), inputContext, chr);
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
}

void TableState::commitBuffer(bool commitCode, bool noRealCommit) {
    auto context = context_.get();
    auto sentence = context->selectedSentence();
    TABLE_DEBUG() << "TableState::commitBuffer " << sentence << " "
                  << context->selectedSize();
    for (size_t i = 0; i < context->selectedSize(); i++) {
        auto seg = context->selectedSegment(i);
        if (std::get<bool>(seg)) {
            pushLastCommit(std::get<std::string>(seg));
        }
    }

    if (commitCode) {
        sentence += context->currentCode();
    }

    if (!noRealCommit) {
        ic_->commitString(sentence);
    }
    context->learn();
    context->clear();
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
            auto candidateList = new CommonCandidateList;
            size_t idx = 0;
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
                candidateList->append(
                    new TableCandidateWord(engine_, std::move(text), idx));
                idx++;
            }
            candidateList->setSelectionKey(*config.selection);
            candidateList->setPageSize(*config.pageSize);
            candidateList->setGlobalCursorIndex(0);
            inputPanel.setCandidateList(candidateList);
        }
        Text preeditText = context->preeditText();
        if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
            inputPanel.setClientPreedit(preeditText);
        } else {
            inputPanel.setPreedit(preeditText);
        }
    }
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}
}
