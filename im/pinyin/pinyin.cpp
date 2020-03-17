//
// Copyright (C) 2017~2017 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//

#include "pinyin.h"
#include "cloudpinyin_public.h"
#include "config.h"
#include "notifications_public.h"
#include "pinyinhelper_public.h"
#include "punctuation_public.h"
#include "spell_public.h"
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
#include <fcitx/action.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputpanel.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <libime/core/historybigram.h>
#include <libime/core/prediction.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindecoder.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/shuangpinprofile.h>
#include <quickphrase_public.h>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(pinyin, "pinyin");

#define PINYIN_DEBUG() FCITX_LOGC(pinyin, Debug)
#define PINYIN_ERROR() FCITX_LOGC(pinyin, Error)

bool consumePreifx(std::string_view &view, std::string_view prefix) {
    if (boost::starts_with(view, prefix)) {
        view.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

class PinyinState : public InputContextProperty {
public:
    PinyinState(PinyinEngine *engine) : context_(engine->ime()) {
        context_.setMaxSize(75);
    }

    libime::PinyinContext context_;
    bool lastIsPunc_ = false;
    std::unique_ptr<EventSourceTime> cancelLastEvent_;

    std::vector<std::string> predictWords_;
};

class PinyinPredictCandidateWord : public CandidateWord {
public:
    PinyinPredictCandidateWord(PinyinEngine *engine, std::string word)
        : CandidateWord(Text(word)), engine_(engine), word_(std::move(word)) {}

    void select(InputContext *inputContext) const override {
        inputContext->commitString(word_);
        auto state = inputContext->propertyFor(&engine_->factory());
        state->predictWords_.push_back(word_);
        // Max history size.
        constexpr size_t maxHistorySize = 5;
        if (state->predictWords_.size() > maxHistorySize) {
            state->predictWords_.erase(state->predictWords_.begin(),
                                       state->predictWords_.begin() +
                                           state->predictWords_.size() -
                                           maxHistorySize);
        }
        engine_->updatePredict(inputContext);
    }

    PinyinEngine *engine_;
    std::string word_;
};

class StrokeCandidateWord : public CandidateWord {
public:
    StrokeCandidateWord(PinyinEngine *engine, const std::string &hz,
                        const std::string &py)
        : CandidateWord(), engine_(engine), hz_(std::move(hz)) {
        if (py.empty()) {
            setText(Text(hz_));
        } else {
            setText(Text(fmt::format(_("{0} ({1})"), hz_, py)));
        }
    }

    void select(InputContext *inputContext) const override {
        inputContext->commitString(hz_);
        engine_->doReset(inputContext);
    }

private:
    PinyinEngine *engine_;
    std::string hz_;
};

class SpellCandidateWord : public CandidateWord {
public:
    SpellCandidateWord(PinyinEngine *engine, const std::string &word)
        : CandidateWord(), engine_(engine), word_(std::move(word)) {
        setText(Text(word_));
    }

    void select(InputContext *inputContext) const override {
        auto state = inputContext->propertyFor(&engine_->factory());
        auto &context = state->context_;
        inputContext->commitString(context.selectedSentence() + word_);
        engine_->doReset(inputContext);
    }

private:
    PinyinEngine *engine_;
    std::string word_;
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

std::unique_ptr<CandidateList>
PinyinEngine::predictCandidateList(const std::vector<std::string> &words) {
    if (words.empty()) {
        return nullptr;
    }
    auto candidateList = std::make_unique<CommonCandidateList>();
    for (const auto &word : words) {
        candidateList->append<PinyinPredictCandidateWord>(this, word);
    }
    candidateList->setSelectionKey(selectionKeys_);
    candidateList->setPageSize(*config_.pageSize);
    candidateList->setGlobalCursorIndex(0);
    return candidateList;
}

void PinyinEngine::initPredict(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    auto lmState = context.state();
    state->predictWords_ = context.selectedWords();
    auto words = prediction_.predict(lmState, context.selectedWords(),
                                     *config_.predictionSize);
    if (auto candidateList = predictCandidateList(words)) {
        auto &inputPanel = inputContext->inputPanel();
        inputPanel.setCandidateList(std::move(candidateList));
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::updatePredict(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto words = prediction_.predict(state->predictWords_, *config_.pageSize);
    if (auto candidateList = predictCandidateList(words)) {
        auto &inputPanel = inputContext->inputPanel();
        inputPanel.setCandidateList(std::move(candidateList));
    }
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

int englishNess(const std::string &input, bool sp) {
    auto pys = stringutils::split(input, " ");
    constexpr int fullWeight = -2, shortWeight = 3, invalidWeight = 6;
    int weight = 0;
    for (auto iter = pys.begin(), end = pys.end(); iter != end; ++iter) {
        if (sp) {
            if (iter->size() == 2) {
                weight += fullWeight;
            } else {
                if (std::next(iter) == end) {
                    weight += shortWeight;
                } else {
                    weight += invalidWeight;
                }
            }
        } else {
            if (*iter == "ng") {
                weight += fullWeight;
            } else {
                auto firstChr = (*iter)[0];
                if (firstChr == '\'') {
                    return 0;
                } else if (firstChr == 'i' || firstChr == 'u' ||
                           firstChr == 'v') {
                    weight += invalidWeight;
                } else if (iter->size() <= 2) {
                    weight += shortWeight;
                } else if (iter->find_first_of("aeiou") != std::string::npos) {
                    weight += fullWeight;
                } else {
                    weight += shortWeight;
                }
            }
        }
    }

    if (weight < 0) {
        return 0;
    }
    return (weight + 3) / 10;
}

bool isStroke(const std::string &input) {
    static const std::unordered_set<char> py{'h', 'p', 's', 'z', 'n'};
    return std::all_of(input.begin(), input.end(),
                       [](char c) { return py.count(c); });
}

void PinyinEngine::updateUI(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    auto &context = state->context_;
    if (context.selected()) {
        auto sentence = context.sentence();
        if (!inputContext->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive)) {
            context.learn();
        }
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        inputContext->commitString(sentence);
        if (*config_.predictionEnabled) {
            initPredict(inputContext);
        }
        context.clear();
        return;
    }

    if (context.userInput().size()) {
        auto &candidates = context.candidates();
        auto &inputPanel = inputContext->inputPanel();
        if (context.candidates().size()) {
            auto candidateList = std::make_unique<CommonCandidateList>();
            size_t idx = 0;
            candidateList->setPageSize(*config_.pageSize);
            candidateList->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);

            std::unique_ptr<CloudPinyinCandidateWord> cloud;
            if (*config_.cloudPinyinEnabled && cloudpinyin() &&
                !inputContext->capabilityFlags().testAny(
                    CapabilityFlag::PasswordOrSensitive)) {
                using namespace std::placeholders;
                auto fullPinyin =
                    context.useShuangpin()
                        ? context.candidateFullPinyin(0)
                        : context.userInput().substr(context.selectedLength());
                cloud = std::make_unique<CloudPinyinCandidateWord>(
                    cloudpinyin(), fullPinyin, context.selectedSentence(),
                    inputContext,
                    std::bind(&PinyinEngine::cloudPinyinSelected, this, _1, _2,
                              _3));
            }
            for (const auto &candidate : candidates) {
                auto candidateString = candidate.toString();
                if (cloud && cloud->filled() &&
                    cloud->word() == candidateString) {
                    cloud.reset();
                }
                candidateList->append<PinyinCandidateWord>(
                    this, Text(std::move(candidateString)), idx);
                idx++;
            }
            int engNess;
            auto parsedPy =
                context.preedit().substr(context.selectedSentence().size());
            if (spell() &&
                (engNess = englishNess(parsedPy, context.useShuangpin()))) {
                auto py = context.userInput().substr(context.selectedLength());
                auto results = spell()->call<ISpell::hintWithProvider>(
                    "en", SpellProvider::Custom, py, engNess);
                int idx = 1;
                for (auto &result : results) {
                    auto actualIdx = idx;
                    if (actualIdx > candidateList->totalSize()) {
                        actualIdx = candidateList->totalSize();
                    }
                    if (cloud && cloud->filled() && cloud->word() == result) {
                        cloud.reset();
                    }

                    candidateList->insert(
                        actualIdx,
                        std::make_unique<SpellCandidateWord>(this, result));
                    idx++;
                }
            }
            // if we didn't got it from cache or whatever, and not empty
            // otherwise we can throw it away.
            if (cloud && (!cloud->filled() || !cloud->word().empty())) {
                auto index = *config_.cloudPinyinIndex;
                if (index >= candidateList->totalSize()) {
                    index = candidateList->totalSize();
                }
                candidateList->insert(index - 1, std::move(cloud));
            }
            if (pinyinhelper() && context.selectedLength() == 0 &&
                isStroke(context.userInput())) {
                int limit = (context.userInput().size() + 4) / 5;
                if (limit > 3) {
                    limit = 3;
                }
                auto results =
                    pinyinhelper()->call<IPinyinHelper::lookupStroke>(
                        context.userInput(), limit);
                int desiredPos =
                    *config_.pageSize - static_cast<int>(results.size());
                if (desiredPos < 0) {
                    desiredPos = 0;
                }
                for (auto &result : results) {
                    utf8::getChar(result.first);
                    auto py = pinyinhelper()->call<IPinyinHelper::lookup>(
                        utf8::getChar(result.first));
                    auto pystr = stringutils::join(py, " ");

                    if (desiredPos > candidateList->size()) {
                        desiredPos = candidateList->size();
                    }
                    candidateList->insert(desiredPos,
                                          std::make_unique<StrokeCandidateWord>(
                                              this, result.first, pystr));
                }
            }
            candidateList->setSelectionKey(selectionKeys_);
            candidateList->setGlobalCursorIndex(0);
            inputPanel.setCandidateList(std::move(candidateList));
        }
        auto preeditWithCursor = context.preeditWithCursor();
        Text preedit(preeditWithCursor.first);
        preedit.setCursor(preeditWithCursor.second);
        if (config_.showPreeditInApplication.value() &&
            inputContext->capabilityFlags().test(CapabilityFlag::Preedit)) {
            inputPanel.setClientPreedit(preedit);
        } else {
            inputPanel.setPreedit(preedit);
            inputPanel.setClientPreedit(
                Text(context.sentence(), TextFormatFlag::Underline));
        }
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
    prediction_.setUserLanguageModel(ime_->model());

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
        } catch (const std::exception &e) {
            PINYIN_ERROR() << "Failed to load pinyin dict: " << e.what();
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
        } catch (const std::exception &e) {
            PINYIN_ERROR() << "Failed to load pinyin history: " << e.what();
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

    predictionAction_.setShortText(_("Prediction"));
    predictionAction_.setLongText(_("Show prediction words"));
    predictionAction_.setIcon(*config_.predictionEnabled
                                  ? "fcitx-remind-active"
                                  : "fcitx-remind-inactive");
    predictionAction_.connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.predictionEnabled.setValue(!(*config_.predictionEnabled));
            predictionAction_.setIcon(*config_.predictionEnabled
                                          ? "fcitx-remind-active"
                                          : "fcitx-remind-inactive");
            predictionAction_.update(ic);
        });
    instance_->userInterfaceManager().registerAction("pinyin-prediction",
                                                     &predictionAction_);
}

PinyinEngine::~PinyinEngine() {}

void PinyinEngine::loadDict(const StandardPathFile &file) {
    if (file.fd() < 0) {
        return;
    }
    try {
        boost::iostreams::stream_buffer<
            boost::iostreams::file_descriptor_source>
            buffer(file.fd(),
                   boost::iostreams::file_descriptor_flags::never_close_handle);
        std::istream in(&buffer);
        ime_->dict()->addEmptyDict();
        ime_->dict()->load(ime_->dict()->dictSize() - 1, in,
                           libime::PinyinDictFormat::Binary);
    } catch (const std::exception &e) {
        PINYIN_ERROR() << "Failed to load pinyin dict " << file.path() << ": "
                       << e.what();
    }
}

void PinyinEngine::loadExtraDict() {
    auto &standardPath = StandardPath::global();
    auto files = standardPath.multiOpen(StandardPath::Type::PkgData,
                                        "pinyin/dictionaries", O_RDONLY,
                                        filter::Suffix(".dict"));
    ime_->dict()->removeAll();
    if (*config_.emojiEnabled) {
        auto file = standardPath.open(StandardPath::Type::PkgData,
                                      "pinyin/emoji.dict", O_RDONLY);
        loadDict(file);
    }
    for (const auto &file : files) {
        PINYIN_DEBUG() << "Loading extra dictionary: " << file.first;
        loadDict(file.second);
    }
}

void PinyinEngine::reloadConfig() {
    readAsIni(config_, "conf/pinyin.conf");
    ime_->setNBest(*config_.nbest);
    if (*config_.shuangpinProfile == ShuangpinProfileEnum::Custom) {
        auto file = StandardPath::global().open(StandardPath::Type::PkgConfig,
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
        switch (*config_.shuangpinProfile) {
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
    const auto &fuzzyConfig = *config_.fuzzyConfig;
#define SET_FUZZY_FLAG(VAR, ENUM)                                              \
    if (*fuzzyConfig.VAR) {                                                    \
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

    loadExtraDict();
}
void PinyinEngine::activate(const fcitx::InputMethodEntry &entry,
                            fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    // Request full width.
    fullwidth();
    chttrans();
    for (auto actionName : {"chttrans", "punctuation", "fullwidth"}) {
        if (auto action =
                instance_->userInterfaceManager().lookupAction(actionName)) {
            inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                                 action);
        }
    }
    inputContext->statusArea().addAction(StatusGroup::InputMethod,
                                         &predictionAction_);
    auto state = inputContext->propertyFor(&factory_);
    state->context_.setUseShuangpin(entry.uniqueName() == "shuangpin");
}

void PinyinEngine::deactivate(const fcitx::InputMethodEntry &entry,
                              fcitx::InputContextEvent &event) {
    auto inputContext = event.inputContext();
    inputContext->statusArea().clearGroup(StatusGroup::InputMethod);
    if (event.type() == EventType::InputContextSwitchInputMethod) {
        auto state = inputContext->propertyFor(&factory_);
        if (state->context_.size()) {
            inputContext->commitString(state->context_.userInput());
        }
    }
    reset(entry, event);
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

    if (cloudpinyin() && event.key().checkKeyList(
                             cloudpinyin()->call<ICloudPinyin::toggleKey>())) {
        config_.cloudPinyinEnabled.setValue(!*config_.cloudPinyinEnabled);
        safeSaveAsIni(config_, "conf/pinyin.conf");

        notifications()->call<INotifications::showTip>(
            "fcitx-cloudpinyin-toggle", "fcitx", "", _("Cloud Pinyin Status"),
            *config_.cloudPinyinEnabled ? _("Cloud Pinyin is enabled.")
                                        : _("Cloud Pinyin is disabled."),
            -1);
        if (*config_.cloudPinyinEnabled) {
            cloudpinyin()->call<ICloudPinyin::resetError>();
        }
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
                candidateList->candidate(idx).select(inputContext);
            }
            return;
        }

        if (event.key().checkKeyList(*config_.prevPage)) {
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

        if (event.key().checkKeyList(*config_.nextPage)) {
            event.filterAndAccept();
            candidateList->toPageable()->next();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            return;
        }

        if (auto movable = candidateList->toCursorMovable()) {
            if (event.key().checkKeyList(*config_.nextCandidate)) {
                movable->nextCandidate();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return event.filterAndAccept();
            } else if (event.key().checkKeyList(*config_.prevCandidate)) {
                movable->prevCandidate();
                inputContext->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return event.filterAndAccept();
            }
        }
    }

    // In prediction, as long as it's not candidate selection, clear, then
    // fallback
    // to remaining operation.
    if (!state->predictWords_.empty()) {
        state->predictWords_.clear();
        inputContext->inputPanel().reset();
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
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
        event.filterAndAccept();
        if (!state->context_.type(Key::keySymToUTF8(event.key().sym()))) {
            return;
        }
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
            auto candidateList = inputContext->inputPanel().candidateList();
            if (candidateList && candidateList->size()) {
                event.filterAndAccept();
                int idx = candidateList->cursorIndex();
                if (idx < 0) {
                    idx = 0;
                }
                inputContext->inputPanel()
                    .candidateList()
                    ->candidate(idx)
                    .select(inputContext);
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
                    state->cancelLastEvent_ =
                        instance()->eventLoop().addTimeEvent(
                            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 300, 0,
                            [this, ref, puncStr](EventSourceTime *, uint64_t) {
                                if (auto inputContext = ref.get()) {
                                    inputContext->commitString(puncStr);
                                    auto state =
                                        inputContext->propertyFor(&factory_);
                                    state->cancelLastEvent_.reset();
                                }
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
                inputContext->inputPanel().candidateList()->candidate(0).select(
                    inputContext);
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

void PinyinEngine::setSubConfig(const std::string &path, const RawConfig &) {
    if (path == "dictmanager") {
        loadExtraDict();
    } else if (path == "clearuserdict") {
        ime_->dict()->clear(libime::PinyinDictionary::UserDict);
    } else if (path == "clearalldict") {
        ime_->dict()->clear(libime::PinyinDictionary::UserDict);
        ime_->model()->history().clear();
    }
}

void PinyinEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto inputContext = event.inputContext();
    doReset(inputContext);
}

void PinyinEngine::doReset(InputContext *inputContext) {
    auto state = inputContext->propertyFor(&factory_);
    state->context_.clear();
    state->predictWords_.clear();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    // state->lastIsPunc_ = false;
}

void PinyinEngine::save() {
    safeSaveAsIni(config_, "conf/pinyin.conf");
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
            } catch (const std::exception &e) {
                PINYIN_ERROR() << "Failed to save pinyin dict: " << e.what();
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
            } catch (const std::exception &e) {
                PINYIN_ERROR() << "Failed to save pinyin history: " << e.what();
                return false;
            }
        });
}

void PinyinEngine::cloudPinyinSelected(InputContext *inputContext,
                                       const std::string &selected,
                                       const std::string &word) {
    auto state = inputContext->propertyFor(&factory_);
    auto words = state->context_.selectedWords();
    auto preedit = state->context_.preedit();
    do {
        if (!stringutils::startsWith(preedit, selected)) {
            break;
        }
        preedit = preedit.substr(selected.size());
        auto pinyins = stringutils::split(preedit, " '");
        std::string_view wordView = word;
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
                words.push_back(std::string{wordView});
            }
            ime_->model()->history().add(words);
        } catch (const std::exception &e) {
        }
    } while (0);
    state->context_.clear();
    inputContext->commitString(selected + word);
    inputContext->inputPanel().reset();
    if (*config_.predictionEnabled) {
        state->predictWords_ = words;
        updatePredict(inputContext);
    }

    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PinyinEngineFactory)
