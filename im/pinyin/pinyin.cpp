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

#include "pinyin.h"
#include "../modules/common.h"
#include "config.h"
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputpanel.h>
#include <fcntl.h>
#include <libime/historybigram.h>
#include <libime/pinyincontext.h>
#include <libime/pinyindictionary.h>
#include <libime/userlanguagemodel.h>

namespace fcitx {

class PinyinState : public InputContextProperty {
public:
    PinyinState(PinyinEngine *engine) : context_(engine->ime()) {}

    libime::PinyinContext context_;
};

class PinyinCandidateWord : public CandidateWord {
public:
    PinyinCandidateWord(PinyinEngine *engine, Text text, size_t idx)
        : CandidateWord(text), engine_(engine), idx_(idx) {}

    void select(InputContext *inputContext) const override {
        auto state = inputContext->propertyFor(engine_->state());
        auto &context = state->context_;
        if (idx_ >= context.candidates().size()) {
            return;
        }
        context.select(idx_);
        engine_->updateUI(inputContext);
    }

    PinyinEngine *engine_;
    size_t idx_;
};

void PinyinEngine::updateUI(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    if (context.selected()) {
        inputContext->commitString(context.sentence());
        context.learn();
        context.clear();
    } else {
        auto &candidates = context.candidates();
        auto &inputPanel = inputContext->inputPanel();
        if (context.candidates().size()) {
            auto candidateList = new CommonCandidateList;
            size_t idx = 0;
            for (const auto &candidate : candidates) {
                candidateList->append(new PinyinCandidateWord(
                    this, Text(candidate.toString()), idx));
                idx++;
            }
            candidateList->setSelectionKey(selectionKeys_);
            inputPanel.setCandidateList(candidateList);
        }
        inputPanel.setClientPreedit(Text(context.sentence()));
        auto preeditWithCursor = context.preeditWithCursor();
        Text preedit(preeditWithCursor.first);
        preedit.setCursor(preeditWithCursor.second);
        inputPanel.setPreedit(Text(preedit));
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

PinyinEngine::PinyinEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &) { return new PinyinState(this); }) {
    ime_ = std::make_unique<libime::PinyinIME>(
        std::make_unique<libime::PinyinDictionary>(),
        std::make_unique<libime::UserLanguageModel>(LIBIME_INSTALL_PKGDATADIR
                                                    "/sc.lm"));
    ime_->dict()->load(libime::PinyinDictionary::SystemDict,
                       LIBIME_INSTALL_PKGDATADIR "/sc.dict",
                       libime::PinyinDictFormat::Binary);

    auto &standardPath = StandardPath::global();
    do {
        auto file = standardPath.openUser(StandardPath::Type::Data,
                                          "fcitx5/pinyin/user.dict", O_RDONLY);

        if (file.fd() < 0) {
            break;
        }

        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                      never_close_handle);
            std::istream in(&buffer);
            ime_->dict()->load(libime::PinyinDictionary::UserDict, in,
                               libime::PinyinDictFormat::Binary);
        } catch (const std::exception &) {
        }
    } while (0);
    do {
        auto file = standardPath.openUser(
            StandardPath::Type::Data, "fcitx5/pinyin/user.history", O_RDONLY);

        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                      never_close_handle);
            std::istream in(&buffer);
            ime_->model()->load(in);
        } catch (const std::exception &) {
        }
    } while (0);

    ime_->setScoreFilter(1);
    reloadConfig();

    instance_->inputContextManager().registerProperty("pinyinState", &factory_);

    KeySym syms[] = {
        FcitxKey_1, FcitxKey_2, FcitxKey_3, FcitxKey_4, FcitxKey_5,
        FcitxKey_6, FcitxKey_7, FcitxKey_8, FcitxKey_9, FcitxKey_0,
    };

    KeyStates states;
    for (auto sym : syms) {
        selectionKeys_.emplace_back(sym, states);
    }
}

PinyinEngine::~PinyinEngine() {}

std::vector<InputMethodEntry> PinyinEngine::listInputMethods() {
    std::vector<InputMethodEntry> result;
    result.emplace_back(std::move(
        InputMethodEntry("pinyin", _("Pinyin Input Method"), "zh_CN", "pinyin")
            .setIcon("pinyin")
            .setLabel("拼")));
    return result;
}

void PinyinEngine::reloadConfig() {
    auto &standardPath = StandardPath::global();
    auto file = standardPath.open(StandardPath::Type::Config,
                                  "fcitx5/conf/pinyin.conf", O_RDONLY);
    RawConfig config;
    readFromIni(config, file.fd());
    config_.load(config);
    ime_->setNBest(config_.nbest.value());
    ime_->setFrameSize(0);
}

void PinyinEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    auto inputContext = event.inputContext();
    // check if we can select candidate.
    if (inputContext->inputPanel().candidateList()) {
        int idx = event.key().keyListIndex(selectionKeys_);
        if (idx >= 0 &&
            idx < inputContext->inputPanel().candidateList()->size()) {
            event.filterAndAccept();
            inputContext->inputPanel().candidateList()->candidate(idx).select(
                inputContext);
            return;
        }
    }

    if (event.key().checkKeyList(config_.prevPage.value())) {
        event.filterAndAccept();
        if (inputContext->inputPanel().candidateList()) {
            inputContext->inputPanel().candidateList()->toPageable()->prev();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
        }
        return;
    }

    if (event.key().checkKeyList(config_.nextPage.value())) {
        event.filterAndAccept();
        if (inputContext->inputPanel().candidateList()) {
            inputContext->inputPanel().candidateList()->toPageable()->next();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
        }
        return;
    }

    auto state = inputContext->propertyFor(&factory_);

    if (event.key().isLAZ() ||
        (event.key().check(FcitxKey_apostrophe) && state->context_.size())) {
        state->context_.type(
            utf8::UCS4ToUTF8(Key::keySymToUnicode(event.key().sym())));
        event.filterAndAccept();
    } else if (state->context_.size()) {
        // key to handle when it is not empty.
        if (event.key().check(FcitxKey_BackSpace)) {
            if (state->context_.selectedLength()) {
                state->context_.cancel();
            } else {
                state->context_.backspace();
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Delete)) {
            state->context_.del();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Home)) {
            state->context_.setCursor(state->context_.selectedLength());
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_End)) {
            state->context_.setCursor(state->context_.size());
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Left)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.cursor();
            if (cursor > 0) {
                state->context_.setCursor(cursor - 1);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right)) {
            auto cursor = state->context_.cursor();
            if (cursor < state->context_.size()) {
                state->context_.setCursor(cursor + 1);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Escape)) {
            state->context_.clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Return)) {
            inputContext->commitString(state->context_.userInput());
            state->context_.clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_space)) {
            if (inputContext->inputPanel().candidateList()->size()) {
                event.filterAndAccept();
                inputContext->inputPanel().candidateList()->candidate(0).select(
                    inputContext);
                return;
            }
        }
    }

    if (event.filtered() && event.accepted()) {
        updateUI(inputContext);
    }
}

void PinyinEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto inputContext = event.inputContext();

    auto state = inputContext->propertyFor(&factory_);
    state->context_.clear();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::save() {
    auto &standardPath = StandardPath::global();
    standardPath.safeSave(
        StandardPath::Type::Data, "fcitx5/pinyin/user.dict", [this](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            ime_->dict()->save(libime::PinyinDictionary::UserDict, out);
            return true;
        });
    standardPath.safeSave(
        StandardPath::Type::Data, "fcitx5/pinyin/user.history", [this](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            ime_->model()->save(out);
            return true;
        });
}
}

FCITX_ADDON_FACTORY(fcitx::PinyinEngineFactory)
