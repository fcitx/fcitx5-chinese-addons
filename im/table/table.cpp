/*
* Copyright (C) 2017~2017 by CSSlayer
* wengxt@gmail.com
*
* This library is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 2 of the
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
#include "table.h"
#include "config.h"
#include "context.h"
#include "fullwidth_public.h"
#include "punctuation_public.h"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputpanel.h>
#include <fcntl.h>
#include <libime/core/historybigram.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/table/tablebaseddictionary.h>
#include <quickphrase_public.h>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(table, "table")

#define TABLE_LOG(LEVEL) FCITX_LOGC(table, LEVEL)

bool consumePreifx(boost::string_view &view, boost::string_view prefix) {
    if (boost::starts_with(view, prefix)) {
        view.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

class TableState : public InputContextProperty {
public:
    TableState(InputContext *ic, TableEngine *engine)
        : ic_(ic), engine_(engine) {}

    InputContext *ic_;
    TableEngine *engine_;
    bool lastIsPunc_ = false;

    TableContext *context(const InputMethodEntry *entry) {
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

    void release() {
        lastContext_.clear();
        context_.reset();
    }

private:
    std::string lastContext_;
    std::unique_ptr<TableContext> context_;
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
        context->select(idx_);
        engine_->updateUI(nullptr, inputContext);
    }

    TableEngine *engine_;
    size_t idx_;
};

void TableEngine::updateUI(const InputMethodEntry *entry,
                           InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto *context = state->context(entry);
    if (!context) {
        return;
    }
    auto &config = context->config();
    if (context->userInput().size()) {
        auto &candidates = context->candidates();
        auto &inputPanel = inputContext->inputPanel();
        if (context->candidates().size()) {
            auto candidateList = new CommonCandidateList;
            size_t idx = 0;

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
                    text.append(" ");
                    text.append(hint);
                }
                candidateList->append(
                    new TableCandidateWord(this, std::move(text), idx));
                idx++;
            }
            // TODO: use real size
            candidateList->setSelectionKey(*config.selection);
            candidateList->setPageSize(*config.pageSize);
            inputPanel.setCandidateList(candidateList);
        }
        Text preeditText = context->preeditText();
        if (inputContext->capabilityFlags().test(CapabilityFlag::Preedit)) {
            inputPanel.setClientPreedit(preeditText);
        } else {
            inputPanel.setPreedit(preeditText);
        }
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

TableEngine::TableEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &ic) { return new TableState(&ic, this); }) {
    ime_ = std::make_unique<TableIME>(
        &libime::DefaultLanguageModelResolver::instance());

    reloadConfig();
    instance_->inputContextManager().registerProperty("tableState", &factory_);
}

TableEngine::~TableEngine() {}

void TableEngine::reloadConfig() {}

void TableEngine::activate(const fcitx::InputMethodEntry &entry,
                           fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    auto context = state->context(&entry);
    if (context && *context->config().useFullWidth && fullwidth()) {
        fullwidth()->call<IFullwidth::enable>("pinyin");
    }
}

void TableEngine::deactivate(const fcitx::InputMethodEntry &entry,
                             fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    if (auto context = state->context(&entry)) {
        if (context->selected()) {
        }
        state->release();
    }
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

std::string TableEngine::subMode(const fcitx::InputMethodEntry &entry,
                                 fcitx::InputContext &ic) {
    auto state = ic.propertyFor(&factory_);
    if (!state->context(&entry)) {
        return _("Not available");
    }
    return {};
}

void TableEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    TABLE_LOG(Debug) << "Table receive key: " << event.key() << " "
                     << event.isRelease();

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    bool needUpdate = false;
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    bool lastIsPunc = state->lastIsPunc_;
    auto context = state->context(&entry);
    if (!context) {
        return;
    }

    state->lastIsPunc_ = false;
    auto &config = context->config();
    // check if we can select candidate.
    auto candidateList = inputContext->inputPanel().candidateList();
    if (candidateList) {
        int idx = event.key().keyListIndex(*config.selection);
        if (idx >= 0) {
            event.filterAndAccept();
            if (idx < candidateList->size()) {
                candidateList->candidate(idx)->select(inputContext);
            }
            return;
        }

        if (event.key().checkKeyList(*config.prevPage)) {
            auto pageable = candidateList->toPageable();
            if (!pageable->hasPrev()) {
                if (pageable->usedNextBefore()) {
                    event.filterAndAccept();
                    return;
                }
            } else {
                event.filterAndAccept();
                pageable->prev();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
        }

        if (event.key().checkKeyList(*config.nextPage)) {
            event.filterAndAccept();
            candidateList->toPageable()->next();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            return;
        }
    }

    auto chr = Key::keySymToUnicode(event.key().sym());
    if (!event.key().hasModifier() && context->isValidInput(chr)) {
        context->type(utf8::UCS4ToUTF8(chr));
        event.filterAndAccept();
    } else if (context->size()) {
        if (event.key().check(FcitxKey_Return)) {
            inputContext->commitString(context->userInput());
            context->clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Escape)) {
            context->clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_BackSpace)) {
            context->backspace();
            event.filterAndAccept();
        } else if (!context->selected()) {
            // key to handle when it is not empty.
            if (event.key().check(FcitxKey_Delete)) {
                event.filterAndAccept();
            } else if (event.key().isCursorMove()) {
                event.filterAndAccept();
            } else if (event.key().check(FcitxKey_space)) {
                if (inputContext->inputPanel().candidateList() &&
                    inputContext->inputPanel().candidateList()->size()) {
                    event.filterAndAccept();
                    inputContext->inputPanel()
                        .candidateList()
                        ->candidate(0)
                        ->select(inputContext);
                    return;
                }
            }
        }
    } else {
        if (event.key().check(FcitxKey_BackSpace)) {
            if (lastIsPunc) {
                auto puncStr = punctuation()->call<IPunctuation::cancelLast>(
                    entry.languageCode(), inputContext);
                if (!puncStr.empty()) {
                    // forward the original key is the best choice.
                    auto ref = inputContext->watch();
                    instance()->eventLoop().addTimeEvent(
                        CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 300, 0,
                        [ref, puncStr](EventSourceTime *e, uint64_t) {
                            if (auto inputContext = ref.get()) {
                                inputContext->commitString(puncStr);
                            }
                            delete e;
                            return true;
                        });
                    event.filter();
                    return;
                }
            }
        }
    }
    if (!event.filtered()) {
        if (event.key().hasModifier()) {
            return;
        }
        // if it gonna commit something
        if (chr) {
            context->autoSelect();
            if (context->selected()) {
                inputContext->commitString(context->selectedSentence());
                context->learn();
                context->clear();
                needUpdate = true;
            }
            auto punc = punctuation()->call<IPunctuation::pushPunctuation>(
                entry.languageCode(), inputContext, chr);
            if (event.key().check(*config.quickphrase) && quickphrase()) {
                auto s = punc.size() ? punc : utf8::UCS4ToUTF8(chr);
                auto alt = punc.size() ? utf8::UCS4ToUTF8(chr) : "";
                std::string text;
                if (s.size()) {
                    text += alt + _(" for ") + s;
                }
                if (alt.size()) {
                    text += _(" Return for ") + alt;
                }
                quickphrase()->call<IQuickPhrase::trigger>(
                    inputContext, text, "", s, alt, Key(FcitxKey_semicolon));
                event.filterAndAccept();
                return;
            }

            if (punc.size()) {
                event.filterAndAccept();
                inputContext->commitString(punc);
            }
            state->lastIsPunc_ = true;
        }
    }

    if ((event.filtered() && event.accepted()) || needUpdate) {
        updateUI(&entry, inputContext);
    }
}

void TableEngine::reset(const InputMethodEntry &entry,
                        InputContextEvent &event) {
    TABLE_LOG(Debug) << "TableEngine::reset";
    auto inputContext = event.inputContext();

    auto state = inputContext->propertyFor(&factory_);
    auto context = state->context(&entry);
    if (context) {
        context->clear();
    }
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    state->lastIsPunc_ = false;
}

void TableEngine::save() {}
}

FCITX_ADDON_FACTORY(fcitx::TableEngineFactory)
