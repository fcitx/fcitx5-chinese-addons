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
#include "context.h"
#include <quickphrase_public.h>

namespace fcitx {

bool consumePreifx(boost::string_view &view, boost::string_view prefix) {
    if (boost::starts_with(view, prefix)) {
        view.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

class TableState : public InputContextProperty {
public:
    TableState(InputContext* ic, TableEngine *engine) : ic_(ic), engine_(engine) {}

    InputContext *ic_;
    TableEngine *engine_;
    bool lastIsPunc_ = false;

    TableContext *context() {
        auto entry = engine_->instance()->inputMethodEntry(ic_);
        if (!entry) {
            return nullptr;
        }
        if (lastContext_ == entry->name()) {
            return context_.get();
        }

        auto dict =engine_->ime()->requestDict(entry->name());
        if (!dict) {
            return nullptr;
        }
        auto lm = engine_->ime()->languageModelForDictionary(dict);
        auto &config = engine_->ime()->config(entry->name());
        context_ = std::make_unique<TableContext>(*dict, config, *lm);
        lastContext_ = entry->name();
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
        auto *context = state->context();
        if (!context || idx_ >= context->candidates().size()) {
            return;
        }
        context->select(idx_);
        engine_->updateUI(inputContext);
    }

    TableEngine *engine_;
    size_t idx_;
};

void TableEngine::updateUI(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto *context = state->context();
    if (!context) {
        return;
    }
    if (context->selected()) {
        auto sentence = context->sentence();
        context->learn();
        context->clear();
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        inputContext->commitString(sentence);
        return;
    }

    if (context->userInput().size()) {
        auto &candidates = context->candidates();
        auto &inputPanel = inputContext->inputPanel();
        if (context->candidates().size()) {
            auto candidateList = new CommonCandidateList;
            size_t idx = 0;

            for (const auto &candidate : candidates) {
                auto candidateString = candidate.toString();
                candidateList->append(new TableCandidateWord(
                    this, Text(std::move(candidateString)), idx));
                idx++;
            }
            // TODO: use real size
            candidateList->setPageSize(*context->config().pageSize);
            inputPanel.setCandidateList(candidateList);
        }
        inputPanel.setClientPreedit(Text(context->sentence()));
        auto preedit = context->preedit();
        Text preeditText(preedit);
        preeditText.setCursor(preedit.size());
        inputPanel.setPreedit(preeditText);
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

std::vector<InputMethodEntry> TableEngine::listInputMethods() {
    std::vector<InputMethodEntry> result;
    result.push_back(std::move(
        InputMethodEntry("wbx", _("Table Input Method"), "zh_CN", "wbx")
            .setIcon("wbx")
            .setLabel("五")));
    return result;
}

void TableEngine::reloadConfig() {
}

void TableEngine::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    auto context = state->context();
    if (context && *context->config().useFullWidth && fullwidth()) {
        fullwidth()->call<IFullwidth::enable>("pinyin");
    }
}

void TableEngine::deactivate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    state->release();
}

void TableEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    FCITX_LOG(Debug) << "Table receive key: " << event.key() << " "
                     << event.isRelease();

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    bool lastIsPunc = state->lastIsPunc_;
    auto context = state->context();
    if (!context) {
        return;
    }

    state->lastIsPunc_ = false;
    auto &config = context->config();
    // check if we can select candidate.
    auto candidateList = inputContext->inputPanel().candidateList();
    if (candidateList) {
        int idx = event.key().keyListIndex(std::vector<Key>());
        if (idx >= 0) {
            event.filterAndAccept();
            if (idx < candidateList->size()) {
                candidateList->candidate(idx)->select(inputContext);
            }
            return;
        }

        if (event.key().checkKeyList(config.prevPage.value())) {
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

        if (event.key().checkKeyList(config.nextPage.value())) {
            event.filterAndAccept();
            candidateList->toPageable()->next();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            return;
        }
    }

    if (event.key().isLAZ()) {
        context->type(
            utf8::UCS4ToUTF8(Key::keySymToUnicode(event.key().sym())));
        event.filterAndAccept();
    } else if (context->size()) {
        // key to handle when it is not empty.
        if (event.key().check(FcitxKey_BackSpace)) {
            context->backspace();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Delete)) {
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Home)) {
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_End)) {
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Left)) {
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right)) {
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Escape)) {
            context->clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Return)) {
            inputContext->commitString(context->userInput());
            context->clear();
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
    } else {
        if (event.key().check(FcitxKey_BackSpace)) {
            if (lastIsPunc) {
                auto puncStr = punctuation()->call<IPunctuation::cancelLast>(
                    "zh_CN", inputContext);
                if (!puncStr.empty()) {
                    // forward the original key is the best choice.
                    inputContext->forwardKey(event.rawKey(), event.isRelease(),
                                             event.time());
                    inputContext->commitString(puncStr);
                    event.filterAndAccept();
                    return;
                }
            }
        }
    }
    if (!event.filtered()) {
        if (event.key().states().testAny(KeyState::SimpleMask)) {
            return;
        }
        // if it gonna commit something
        auto c = Key::keySymToUnicode(event.key().sym());
        if (c) {
            if (inputContext->inputPanel().candidateList() &&
                inputContext->inputPanel().candidateList()->size()) {
                inputContext->inputPanel()
                    .candidateList()
                    ->candidate(0)
                    ->select(inputContext);
            }
            auto punc = punctuation()->call<IPunctuation::pushPunctuation>(
                "zh_CN", inputContext, c);
            if (event.key().check(FcitxKey_semicolon) && quickphrase()) {
                auto s = punc.size() ? punc : utf8::UCS4ToUTF8(c);
                auto alt = punc.size() ? utf8::UCS4ToUTF8(c) : "";
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

    if (event.filtered() && event.accepted()) {
        updateUI(inputContext);
    }
}

void TableEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto inputContext = event.inputContext();

    auto state = inputContext->propertyFor(&factory_);
    auto context = state->context();
    if (context) {
        context->clear();
    }
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    state->lastIsPunc_ = false;
}

void TableEngine::save() {
}

}

FCITX_ADDON_FACTORY(fcitx::TableEngineFactory)
