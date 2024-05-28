/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "engine.h"
#include "config.h"
#include "context.h"
#include "ime.h"
#include "state.h"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <cstddef>
#include <exception>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <istream>
#include <libime/core/historybigram.h>
#include <libime/core/languagemodel.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinencoder.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <libime/table/tablebaseddictionary.h>
#include <map>
#include <memory>
#include <quickphrase_public.h>
#include <string>
#include <string_view>
#include <unordered_set>

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
            releaseStates();
            std::unordered_set<std::string> names;
            for (const auto &im : instance_->inputMethodManager()
                                      .currentGroup()
                                      .inputMethodList()) {
                names.insert(im.name());
            }
            ime_->releaseUnusedDict(names);
            preload();
        }));
    events_.emplace_back(instance_->watchEvent(
        EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
        [this](Event &event) {
            auto &keyEvent = static_cast<KeyEvent &>(event);
            auto *inputContext = keyEvent.inputContext();
            const auto *entry = instance_->inputMethodEntry(inputContext);
            if (!entry || entry->addon() != "table") {
                return;
            }
            auto *state = inputContext->propertyFor(&factory_);
            state->handle2nd3rdCandidate(keyEvent);
        }));

    predictionAction_.setShortText(*config_.predictionEnabled
                                       ? _("Prediction Enabled")
                                       : _("Prediction Disabled"));
    predictionAction_.setLongText(_("Show prediction words"));
    predictionAction_.connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.predictionEnabled.setValue(!(*config_.predictionEnabled));
            saveConfig();
            predictionAction_.setShortText(*config_.predictionEnabled
                                               ? _("Prediction Enabled")
                                               : _("Prediction Disabled"));
            predictionAction_.setIcon(*config_.predictionEnabled
                                          ? "fcitx-remind-active"
                                          : "fcitx-remind-inactive");
            predictionAction_.update(ic);
        });
    instance_->userInterfaceManager().registerAction("table-prediction",
                                                     &predictionAction_);

    preloadEvent_ = instance_->eventLoop().addDeferEvent([this](EventSource *) {
        preload();
        preloadEvent_.reset();
        return true;
    });
}

TableEngine::~TableEngine() = default;

void TableEngine::reloadConfig() {
    readAsIni(config_, "conf/table.conf");
    populateConfig();
}

void TableEngine::populateConfig() {
    reverseShuangPinTable_.reset();

    std::unique_ptr<libime::ShuangpinProfile> shuangpinProfile;

    if (*config_.shuangpinProfile == LookupShuangpinProfileEnum::Custom) {
        auto file = StandardPath::global().open(StandardPath::Type::PkgConfig,
                                                "pinyin/sp.dat", O_RDONLY);
        if (file.isValid()) {
            try {
                boost::iostreams::stream_buffer<
                    boost::iostreams::file_descriptor_source>
                    buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                          never_close_handle);
                std::istream in(&buffer);
                shuangpinProfile =
                    std::make_unique<libime::ShuangpinProfile>(in);
            } catch (const std::exception &e) {
                TABLE_ERROR() << e.what();
            }
        } else {
            TABLE_ERROR() << "Failed to open shuangpin profile.";
        }
    } else {
        libime::ShuangpinBuiltinProfile profile =
            libime::ShuangpinBuiltinProfile::Ziranma;
#define TRANS_SP_PROFILE(PROFILE)                                              \
    case LookupShuangpinProfileEnum::PROFILE:                                  \
        profile = libime::ShuangpinBuiltinProfile::PROFILE;                    \
        break;
        switch (*config_.shuangpinProfile) {
            TRANS_SP_PROFILE(Ziranma)
            TRANS_SP_PROFILE(MS)
            TRANS_SP_PROFILE(Ziguang)
            TRANS_SP_PROFILE(ABC)
            TRANS_SP_PROFILE(Zhongwenzhixing)
            TRANS_SP_PROFILE(PinyinJiajia)
            TRANS_SP_PROFILE(Xiaohe)
        case LookupShuangpinProfileEnum::No:
        default:
            break;
        }
        shuangpinProfile = std::make_unique<libime::ShuangpinProfile>(profile);
    }

    if (!shuangpinProfile) {
        return;
    }

    reverseShuangPinTable_ =
        std::make_unique<std::multimap<std::string, std::string>>();
    for (const auto &[input, pys] : shuangpinProfile->table()) {
        for (const auto &[syl, fuzzy] : pys) {
            if (fuzzy != libime::PinyinFuzzyFlag::None) {
                continue;
            }
            reverseShuangPinTable_->emplace(syl.toString(), input);
        }
    }
}

void TableEngine::setSubConfig(const std::string &path,
                               const RawConfig & /*unused*/) {
    if (path == "reloaddict") {
        reloadDict();
    }
}

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

    if (state->mode() == TableMode::Punctuation) {
        auto candidateList = inputContext->inputPanel().candidateList();
        if (candidateList && event.type() != EventType::InputContextFocusOut) {
            auto index = candidateList->cursorIndex();
            if (index >= 0) {
                candidateList->candidate(index).select(inputContext);
            }
        }
    } else if (state->context() &&
               *state->context()->config().commitWhenDeactivate) {
        // The reason that we do not commit here is we want to force the
        // behavior. When client get unfocused, the framework will try to commit
        // the string.
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
            } catch (const std::exception &e) {
                TABLE_ERROR() << "Failed to load pinyin dict: " << e.what();
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

void TableEngine::releaseStates() {
    instance_->inputContextManager().foreach([&](InputContext *ic) {
        auto *state = ic->propertyFor(&factory_);
        state->release();
        return true;
    });
}

void TableEngine::reloadDict() {
    releaseStates();
    ime_->reloadAllDict();
}

void TableEngine::preload() {
    if (!instance_->globalConfig().preloadInputMethod()) {
        return;
    }

    auto &imManager = instance_->inputMethodManager();
    const auto &group = imManager.currentGroup();

    // Preload first input method.
    if (!group.inputMethodList().empty()) {
        if (const auto *entry =
                imManager.entry(group.inputMethodList()[0].name());
            entry && entry->addon() == "table") {
            ime_->requestDict(entry->uniqueName());
        }
    }
    // Preload default input method.
    if (!group.defaultInputMethod().empty()) {
        if (const auto *entry = imManager.entry(group.defaultInputMethod());
            entry && entry->addon() == "table") {
            ime_->requestDict(entry->uniqueName());
        }
    }
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::TableEngineFactory)
