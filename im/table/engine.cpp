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
#include "fullwidth_public.h"
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
#include <fcitx/inputpanel.h>
#include <fcntl.h>
#include <libime/core/historybigram.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/table/tablebaseddictionary.h>
#include <quickphrase_public.h>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(table, "table")

#define TABLE_LOG(LEVEL) FCITX_LOGC(table, LEVEL)

TableEngine::TableEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &ic) { return new TableState(&ic, this); }) {
    ime_ = std::make_unique<TableIME>(
        &libime::DefaultLanguageModelResolver::instance());

    reloadConfig();
    instance_->inputContextManager().registerProperty("tableState", &factory_);
}

TableEngine::~TableEngine() {}

void TableEngine::reloadConfig() {
    auto &standardPath = StandardPath::global();
    auto file = standardPath.open(StandardPath::Type::PkgConfig,
                                  "conf/table.conf", O_RDONLY);
    RawConfig config;
    readFromIni(config, file.fd());

    config_.load(config);
}

void TableEngine::activate(const fcitx::InputMethodEntry &entry,
                           fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    auto context = state->context(&entry);
    if (context && *context->config().useFullWidth && fullwidth()) {
        fullwidth()->call<IFullwidth::enable>(entry.uniqueName());
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
    TABLE_LOG(Debug) << "TableEngine::reset";
    auto inputContext = event.inputContext();

    auto state = inputContext->propertyFor(&factory_);
    state->reset(&entry);
}

void TableEngine::save() {}

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
}

FCITX_ADDON_FACTORY(fcitx::TableEngineFactory)
