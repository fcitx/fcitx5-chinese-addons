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
#include "engine.h"
#include "config.h"
#include "context.h"
#include "punctuation_public.h"
#include "state.h"
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
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <libime/core/historybigram.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/table/tablebaseddictionary.h>
#include <quickphrase_public.h>

namespace fcitx {

TableEngine::TableEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &ic) { return new TableState(&ic, this); }) {
    ime_ = std::make_unique<TableIME>(
        &libime::DefaultLanguageModelResolver::instance());

    reloadConfig();
    instance_->inputContextManager().registerProperty("tableState", &factory_);
    event_ = instance_->watchEvent(
        EventType::InputMethodGroupChanged, EventWatcherPhase::Default,
        [this](Event &) {
            instance_->inputContextManager().foreach([this](InputContext *ic) {
                auto state = ic->propertyFor(&factory_);
                state->release();
                return true;
            });
            std::unordered_set<std::string> names;
            for (const auto &im : instance_->inputMethodManager()
                                      .currentGroup()
                                      .inputMethodList()) {
                names.insert(im.name());
            }
            ime_->releaseUnusedDict(names);
        });
}

TableEngine::~TableEngine() {}

void TableEngine::reloadConfig() { readAsIni(config_, "conf/table.conf"); }

void TableEngine::activate(const fcitx::InputMethodEntry &entry,
                           fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    auto context = state->context(&entry);
    if (stringutils::startsWith(entry.languageCode(), "zh_")) {
        for (auto actionName : {"chttrans", "punctuation"}) {
            if (auto action = instance_->userInterfaceManager().lookupAction(
                    actionName)) {
                inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                     action);
            }
        }
    }
    if (context && *context->config().useFullWidth && fullwidth()) {
        if (auto action =
                instance_->userInterfaceManager().lookupAction("fullwidth")) {
            inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                 action);
        }
    }
}

void TableEngine::deactivate(const fcitx::InputMethodEntry &entry,
                             fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    inputContext->statusArea().clearGroup(StatusGroup::InputMethod);
    reset(entry, event);
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
    TABLE_DEBUG() << "Table receive key: " << event.key() << " "
                  << event.isRelease();

    // by pass all key release and by pass all modifier
    if (event.isRelease() || event.key().isModifier()) {
        return;
    }

    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    state->keyEvent(entry, event);
}

void TableEngine::reset(const InputMethodEntry &entry,
                        InputContextEvent &event) {
    TABLE_DEBUG() << "TableEngine::reset";
    auto inputContext = event.inputContext();

    auto state = inputContext->propertyFor(&factory_);
    // The reason that we do not commit here is we want to force the behavior.
    // When client get unfocused, the framework will try to commit the string.
    state->commitBuffer(true, event.type() == EventType::InputContextFocusOut);
    state->reset(&entry);
}

void TableEngine::save() { ime_->saveAll(); }

const libime::PinyinDictionary &TableEngine::pinyinDict() {
    if (!pinyinLoaded_) {
        try {
            pinyinDict_.load(libime::PinyinDictionary::SystemDict,
                             LIBIME_INSTALL_PKGDATADIR "/sc.dict",
                             libime::PinyinDictFormat::Binary);
        } catch (const std::exception &) {
        }
        pinyinLoaded_ = true;
    }
    return pinyinDict_;
}

const libime::LanguageModel &TableEngine::pinyinModel() {
    if (!pinyinLM_) {
        pinyinLM_ = std::make_unique<libime::LanguageModel>(
            libime::DefaultLanguageModelResolver::instance()
                .languageModelFileForLanguage("zh_CN"));
    }
    return *pinyinLM_;
}

const Configuration *
TableEngine::getConfigForInputMethod(const InputMethodEntry &entry) const {
    auto dict = ime_->requestDict(entry.uniqueName());
    return std::get<2>(dict);
}

void TableEngine::setConfigForInputMethod(const InputMethodEntry &entry,
                                          const RawConfig &config) {
    ime_->updateConfig(entry.uniqueName(), config);
}
}

FCITX_ADDON_FACTORY(fcitx::TableEngineFactory)
