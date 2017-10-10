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

#include "pinyin.h"
#include "cloudpinyin_public.h"
#include "config.h"
#include "fullwidth_public.h"
#include "notifications_public.h"
#include "punctuation_public.h"
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
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
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <quickphrase_public.h>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(pinyin, "pinyin");

#define PINYIN_DEBUG() FCITX_LOGC(pinyin, Debug)

bool consumePreifx(boost::string_view &view, boost::string_view prefix) {
    if (boost::starts_with(view, prefix)) {
        view.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

class PinyinState : public InputContextProperty {
public:
    PinyinState(PinyinEngine *engine) : context_(engine->ime()) {}

    libime::PinyinContext context_;
    bool lastIsPunc_ = false;
};

class PinyinCandidateWord : public CandidateWord {
public:
    PinyinCandidateWord(PinyinEngine *engine, Text text, size_t idx)
        : CandidateWord(std::move(text)), engine_(engine), idx_(idx) {}

    void select(InputContext *inputContext) const override {
        auto state = inputContext->propertyFor(&engine_->factory());
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
        auto sentence = context.sentence();
        context.learn();
        context.clear();
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        inputContext->commitString(sentence);
        return;
    }

    if (context.userInput().size()) {
        auto &candidates = context.candidates();
        auto &inputPanel = inputContext->inputPanel();
        if (context.candidates().size()) {
            auto candidateList = new CommonCandidateList;
            size_t idx = 0;

            std::unique_ptr<CloudPinyinCandidateWord> cloud;
            if (config_.cloudPinyinEnabled.value() && cloudpinyin()) {
                using namespace std::placeholders;
                auto fullPinyin =
                    context.useShuangpin()
                        ? context.candidateFullPinyin(0)
                        : context.userInput().substr(context.selectedLength());
                cloud = std::make_unique<CloudPinyinCandidateWord>(
                    cloudpinyin(), fullPinyin, context.selectedSentence(),
                    inputContext, std::bind(&PinyinEngine::cloudPinyinSelected,
                                            this, _1, _2, _3));
            }
            for (const auto &candidate : candidates) {
                auto candidateString = candidate.toString();
                if (cloud && cloud->filled() &&
                    cloud->word() == candidateString) {
                    cloud.reset();
                }
                candidateList->append(new PinyinCandidateWord(
                    this, Text(std::move(candidateString)), idx));
                idx++;
            }
            // if we didn't got it from cache or whatever, and not empty
            // otherwise we can throw it away.
            if (cloud && (!cloud->filled() || !cloud->word().empty())) {
                auto index = config_.cloudPinyinIndex.value();
                if (index >= candidateList->totalSize()) {
                    index = candidateList->totalSize();
                }
                candidateList->insert(index - 1, cloud.release());
            }
            candidateList->setSelectionKey(selectionKeys_);
            candidateList->setPageSize(config_.pageSize.value());
            inputPanel.setCandidateList(candidateList);
        }
        inputPanel.setClientPreedit(
            Text(context.sentence(), TextFormatFlag::Underline));
        auto preeditWithCursor = context.preeditWithCursor();
        Text preedit(preeditWithCursor.first);
        preedit.setCursor(preeditWithCursor.second);
        inputPanel.setPreedit(preedit);
#if 0
        {
            size_t count = 1;
            std::cout << "--------------------------" << std::endl;
            for (auto &candidate : context.candidates()) {
                std::cout << (count % 10) << ": ";
                for (auto node : candidate.sentence()) {
                    std::cout << node->word();
                    std::cout << " ";
                }
                std::cout << " " << candidate.score() << std::endl;
                count++;
                if (count > 20) {
                    break;
                }
            }
        }
#endif
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

PinyinEngine::PinyinEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &) { return new PinyinState(this); }) {
    ime_ = std::make_unique<libime::PinyinIME>(
        std::make_unique<libime::PinyinDictionary>(),
        std::make_unique<libime::UserLanguageModel>(
            libime::DefaultLanguageModelResolver::instance()
                .languageModelFileForLanguage("zh_CN")));
    ime_->dict()->load(libime::PinyinDictionary::SystemDict,
                       LIBIME_INSTALL_PKGDATADIR "/sc.dict",
                       libime::PinyinDictFormat::Binary);

    auto &standardPath = StandardPath::global();
    do {
        auto file = standardPath.openUser(StandardPath::Type::PkgData,
                                          "pinyin/user.dict", O_RDONLY);

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
        auto file = standardPath.openUser(StandardPath::Type::PkgData,
                                          "pinyin/user.history", O_RDONLY);

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

void PinyinEngine::reloadConfig() {
    auto &standardPath = StandardPath::global();
    auto file = standardPath.open(StandardPath::Type::PkgConfig,
                                  "conf/pinyin.conf", O_RDONLY);
    RawConfig config;
    readFromIni(config, file.fd());
    config_.load(config);
    ime_->setNBest(config_.nbest.value());
    if (config_.shuangpinProfile.value() == ShuangpinProfileEnum::Custom) {
        auto file = standardPath.open(StandardPath::Type::PkgConfig,
                                      "pinyin/sp.dat", O_RDONLY);
        try {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(file.fd(), boost::iostreams::file_descriptor_flags::
                                      never_close_handle);
            std::istream in(&buffer);
            ime_->setShuangpinProfile(
                std::make_shared<libime::ShuangpinProfile>(in));
        } catch (const std::exception &) {
        }
    } else {
        libime::ShuangpinBuiltinProfile profile;
#define TRANS_SP_PROFILE(PROFILE)                                              \
    case ShuangpinProfileEnum::PROFILE:                                        \
        profile = libime::ShuangpinBuiltinProfile::PROFILE;                    \
        break;
        switch (config_.shuangpinProfile.value()) {
            TRANS_SP_PROFILE(Ziranma)
            TRANS_SP_PROFILE(MS)
            TRANS_SP_PROFILE(Ziguang)
            TRANS_SP_PROFILE(ABC)
            TRANS_SP_PROFILE(Zhongwenzhixing)
            TRANS_SP_PROFILE(PinyinJiajia)
            TRANS_SP_PROFILE(Xiaohe)
        default:
            profile = libime::ShuangpinBuiltinProfile::Ziranma;
            break;
        }
        ime_->setShuangpinProfile(
            std::make_shared<libime::ShuangpinProfile>(profile));
    }

    libime::PinyinFuzzyFlags flags;
    const auto &fuzzyConfig = config_.fuzzyConfig.value();
#define SET_FUZZY_FLAG(VAR, ENUM)                                              \
    if (fuzzyConfig.VAR.value()) {                                             \
        flags |= libime::PinyinFuzzyFlag::ENUM;                                \
    }
    SET_FUZZY_FLAG(ue, VE_UE)
    SET_FUZZY_FLAG(ng, NG_GN)
    SET_FUZZY_FLAG(inner, Inner)
    SET_FUZZY_FLAG(v, V_U)
    SET_FUZZY_FLAG(an, AN_ANG)
    SET_FUZZY_FLAG(en, EN_ENG)
    SET_FUZZY_FLAG(ian, IAN_IANG)
    SET_FUZZY_FLAG(in, IN_ING)
    SET_FUZZY_FLAG(ou, U_OU)
    SET_FUZZY_FLAG(uan, UAN_UANG)
    SET_FUZZY_FLAG(c, C_CH)
    SET_FUZZY_FLAG(f, F_H)
    SET_FUZZY_FLAG(l, L_N)
    SET_FUZZY_FLAG(s, S_SH)
    SET_FUZZY_FLAG(z, Z_ZH)

    ime_->setFuzzyFlags(flags);
}
void PinyinEngine::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
    if (fullwidth()) {
        fullwidth()->call<IFullwidth::enable>(entry.uniqueName());
    }
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    state->context_.setUseShuangpin(entry.uniqueName() == "shuangpin");
}
void PinyinEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    PINYIN_DEBUG() << "Pinyin receive key: " << event.key() << " "
                   << event.isRelease();

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    if (cloudpinyin() &&
        event.key().checkKeyList(
            cloudpinyin()->call<ICloudPinyin::toggleKey>())) {
        config_.cloudPinyinEnabled.setValue(
            !config_.cloudPinyinEnabled.value());
        safeSaveAsIni(config_, "conf/pinyin.conf");

        notifications()->call<INotifications::showTip>(
            "fcitx-cloudpinyin-toggle", "fcitx", "", _("Cloud Pinyin Status"),
            config_.cloudPinyinEnabled.value() ? _("Cloud Pinyin is enabled.")
                                               : _("Cloud Pinyin is disabled."),
            -1);
        event.filterAndAccept();
        return;
    }

    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    bool lastIsPunc = state->lastIsPunc_;
    state->lastIsPunc_ = false;
    // check if we can select candidate.
    auto candidateList = inputContext->inputPanel().candidateList();
    if (candidateList) {
        int idx = event.key().keyListIndex(selectionKeys_);
        if (idx >= 0) {
            event.filterAndAccept();
            if (idx < candidateList->size()) {
                candidateList->candidate(idx)->select(inputContext);
            }
            return;
        }

        if (event.key().checkKeyList(config_.prevPage.value())) {
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

        if (event.key().checkKeyList(config_.nextPage.value())) {
            event.filterAndAccept();
            candidateList->toPageable()->next();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            return;
        }
    }

    auto checkSp = [this](const KeyEvent &event, PinyinState *state) {
        auto shuangpinProfile = ime_->shuangpinProfile();
        return state->context_.useShuangpin() && shuangpinProfile &&
               event.key().isSimple() &&
               shuangpinProfile->validInput().count(
                   Key::keySymToUnicode(event.key().sym()));
    };

    if (event.key().isLAZ() ||
        (event.key().check(FcitxKey_apostrophe) && state->context_.size()) ||
        (state->context_.size() && checkSp(event, state))) {
        // first v, use it to trigger quickphrase
        if (!state->context_.useShuangpin() && quickphrase() &&
            event.key().check(FcitxKey_v) && !state->context_.size()) {

            quickphrase()->call<IQuickPhrase::trigger>(
                inputContext, "", "v", "", "", Key(FcitxKey_None));
            event.filterAndAccept();
            return;
        }
        state->context_.type(Key::keySymToUTF8(event.key().sym()));
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
        } else if (event.key().check(FcitxKey_Left, KeyState::Ctrl)) {
            if (state->context_.cursor() == state->context_.selectedLength()) {
                state->context_.cancel();
            }
            auto cursor = state->context_.pinyinBeforeCursor();
            if (cursor >= 0) {
                state->context_.setCursor(cursor);
            }
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_Right, KeyState::Ctrl)) {
            auto cursor = state->context_.pinyinAfterCursor();
            if (cursor >= 0 &&
                static_cast<size_t>(cursor) <= state->context_.size()) {
                state->context_.setCursor(cursor);
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
                auto keyString = utf8::UCS4ToUTF8(c);
                // s is punc or key
                auto output = punc.size() ? punc : keyString;
                // alt is key or empty
                auto altOutput = punc.size() ? keyString : "";
                // if no punc: key -> key (s = key, alt = empty)
                // if there's punc: key -> punc, return -> key (s = punc, alt =
                // key)
                std::string text;
                if (!output.empty()) {
                    if (!altOutput.empty()) {
                        text = boost::str(
                            boost::format(
                                _("Press %1% for %2% and %3% for %4%")) %
                            keyString % output % _("Return") % altOutput);
                    } else {
                        text =
                            boost::str(boost::format(_("Press %1% for %2%")) %
                                       keyString % altOutput);
                    }
                }
                quickphrase()->call<IQuickPhrase::trigger>(
                    inputContext, text, "", output, altOutput,
                    Key(FcitxKey_semicolon));
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

void PinyinEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto inputContext = event.inputContext();

    auto state = inputContext->propertyFor(&factory_);
    state->context_.clear();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    state->lastIsPunc_ = false;
}

void PinyinEngine::save() {
    auto &standardPath = StandardPath::global();
    standardPath.safeSave(
        StandardPath::Type::PkgData, "pinyin/user.dict", [this](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            try {
                ime_->dict()->save(libime::PinyinDictionary::UserDict, out,
                                   libime::PinyinDictFormat::Binary);
                return static_cast<bool>(out);
            } catch (const std::exception &) {
                return false;
            }
        });
    standardPath.safeSave(
        StandardPath::Type::PkgData, "pinyin/user.history", [this](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_sink>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            try {
                ime_->model()->save(out);
                return true;
            } catch (const std::exception &) {
                return false;
            }
        });
}

void PinyinEngine::cloudPinyinSelected(InputContext *inputContext,
                                       const std::string &selected,
                                       const std::string &word) {
    auto state = inputContext->propertyFor(&factory_);
    auto preedit = state->context_.preedit();
    do {
        if (!stringutils::startsWith(preedit, selected)) {
            break;
        }
        preedit = preedit.substr(selected.size());
        auto pinyins = stringutils::split(preedit, " '");
        boost::string_view wordView = word;
        auto words = state->context_.selectedWords();
        if (pinyins.empty() || pinyins.size() != utf8::length(word)) {
            break;
        }
        const auto &candidates = state->context_.candidates();
        auto pinyinsIter = pinyins.begin();
        auto pinyinsEnd = pinyins.end();
        if (candidates.size()) {
            const auto &bestSentence = candidates[0].sentence();
            auto iter = bestSentence.begin();
            auto end = bestSentence.end();
            while (iter != end) {
                auto consumed = wordView;
                if (!consumePreifx(consumed, (*iter)->word())) {
                    break;
                }
                if ((*iter)->word().size()) {
                    words.push_back((*iter)->word());
                    PINYIN_DEBUG()
                        << "Cloud Pinyin can reuse segment " << (*iter)->word();
                    auto pinyinNode =
                        static_cast<const libime::PinyinLatticeNode *>(*iter);
                    auto pinyinSize = pinyinNode->encodedPinyin().size() / 2;
                    if (pinyinSize &&
                        static_cast<size_t>(std::distance(
                            pinyinsIter, pinyinsEnd)) >= pinyinSize) {
                        pinyinsIter += pinyinSize;
                    } else {
                        break;
                    }
                }
                wordView = consumed;
                iter++;
            }
        }
        // if pinyin is not valid, it may throw
        try {
            if (utf8::length(wordView) == 1 &&
                std::all_of(words.begin(), words.end(),
                            [](const std::string &w) {
                                return utf8::length(w) == 1;
                            })) {
                words = state->context_.selectedWords();
                auto joined =
                    stringutils::join(pinyins.begin(), pinyins.end(), "'");
                words.push_back(word);
                ime_->dict()->addWord(libime::PinyinDictionary::UserDict,
                                      joined, word);
            } else {
                auto joined = stringutils::join(pinyinsIter, pinyinsEnd, "'");
                ime_->dict()->addWord(libime::PinyinDictionary::UserDict,
                                      joined, wordView);
                words.push_back(wordView.to_string());
            }
            ime_->model()->history().add(words);
        } catch (const std::exception &e) {
        }
    } while (0);
    state->context_.clear();
    inputContext->commitString(selected + word);
    inputContext->inputPanel().reset();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}
}

FCITX_ADDON_FACTORY(fcitx::PinyinEngineFactory)
