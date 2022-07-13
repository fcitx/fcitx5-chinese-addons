/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
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
    events_.emplace_back(instance_->watchEvent(
        EventType::InputMethodGroupChanged, EventWatcherPhase::Default,
        [this](Event &) {
            instance_->inputContextManager().foreach([this](InputContext *ic) {
                auto *state = ic->propertyFor(&factory_);
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
        }));
    events_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            auto *inputContext = keyEvent.inputContext();
            auto *entry = instance_->inputMethodEntry(inputContext);
            if (!entry || entry->addon() != "table") {
                return;
            }
            auto *state = inputContext->propertyFor(&factory_);
            state->handle2nd3rdCandidate(keyEvent);
        }));

    predictionAction_.setShortText(_("Prediction"));
    predictionAction_.setLongText(_("Show prediction words"));
    predictionAction_.connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.predictionEnabled.setValue(!(*config_.predictionEnabled));
            saveConfig();
            predictionAction_.setIcon(*config_.predictionEnabled
                                          ? "fcitx-remind-active"
                                          : "fcitx-remind-inactive");
            predictionAction_.update(ic);
        });
    instance_->userInterfaceManager().registerAction("table-prediction",
                                                     &predictionAction_);
}

TableEngine::~TableEngine() {}

void TableEngine::reloadConfig() { readAsIni(config_, "conf/table.conf"); }

void TableEngine::activate(const fcitx::InputMethodEntry &entry,
                           fcitx::InputContextEvent &event) {
    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    auto *context = state->updateContext(&entry);
    if (stringutils::startsWith(entry.languageCode(), "zh_")) {
        chttrans();
        for (const auto *actionName : {"chttrans", "punctuation"}) {
            if (auto *action = instance_->userInterfaceManager().lookupAction(
                    actionName)) {
                inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                     action);
            }
        }
    }
    if (context && *context->config().useFullWidth && fullwidth()) {
        if (auto *action =
                instance_->userInterfaceManager().lookupAction("fullwidth")) {
            inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                 action);
        }
    }
    if (context && context->prediction()) {
        predictionAction_.setIcon(*config_.predictionEnabled
                                      ? "fcitx-remind-active"
                                      : "fcitx-remind-inactive");
        inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                             &predictionAction_);
    }
}

void TableEngine::deactivate(const fcitx::InputMethodEntry &entry,
                             fcitx::InputContextEvent &event) {
    reset(entry, event);
}

std::string TableEngine::subMode(const fcitx::InputMethodEntry &entry,
                                 fcitx::InputContext &ic) {
    auto *state = ic.propertyFor(&factory_);
    if (!state->updateContext(&entry)) {
        return _("Not available");
    }
    return {};
}

void TableEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    TABLE_DEBUG() << "Table receive key: " << event.key() << " "
                  << event.isRelease();

    auto *inputContext = event.inputContext();
    auto *state = inputContext->propertyFor(&factory_);
    state->keyEvent(entry, event);
}

void TableEngine::reset(const InputMethodEntry &entry,
                        InputContextEvent &event) {
    TABLE_DEBUG() << "TableEngine::reset";
    auto *inputContext = event.inputContext();

    auto *state = inputContext->propertyFor(&factory_);
    // The reason that we do not commit here is we want to force the behavior.
    // When client get unfocused, the framework will try to commit the string.
    if (state->context() && *state->context()->config().commitWhenDeactivate) {
        state->commitBuffer(true,
                            event.type() == EventType::InputContextFocusOut);
    }
    state->reset(&entry);
}

void TableEngine::save() { ime_->saveAll(); }

const libime::PinyinDictionary &TableEngine::pinyinDict() {
    if (!pinyinLoaded_) {
        std::string_view dicts[] = {"sc.dict", "extb.dict"};
        static_assert(FCITX_ARRAY_SIZE(dicts) <=
                      libime::PinyinDictionary::UserDict + 1);
        for (size_t i = 0; i < FCITX_ARRAY_SIZE(dicts); i++) {
            try {
                const auto &standardPath = StandardPath::global();
                auto systemDictFile = standardPath.open(
                    StandardPath::Type::Data,
                    stringutils::joinPath("libime", dicts[i]), O_RDONLY);
                if (!systemDictFile.isValid()) {
                    systemDictFile = standardPath.open(
                        StandardPath::Type::Data,
                        stringutils::joinPath(LIBIME_INSTALL_PKGDATADIR,
                                              dicts[i]),
                        O_RDONLY);
                }

                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(systemDictFile.fd(),
                           boost::iostreams::file_descriptor_flags::
                               never_close_handle);
                std::istream in(&buffer);
                pinyinDict_.load(i, in, libime::PinyinDictFormat::Binary);
            } catch (const std::exception &) {
            }
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
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::TableEngineFactory)
