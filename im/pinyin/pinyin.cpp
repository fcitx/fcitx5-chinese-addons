/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "pinyin.h"
#include "cloudpinyin_public.h"
#include "config.h"
#ifdef FCITX_HAS_LUA
#include "luaaddon_public.h"
#endif
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
#include <libime/pinyin/pinyinencoder.h>
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

enum class PinyinMode { Normal, StrokeFilter, ForgetCandidate };

class PinyinState : public InputContextProperty {
public:
    PinyinState(PinyinEngine *engine) : context_(engine->ime()) {
        context_.setMaxSentenceLength(35);
    }

    libime::PinyinContext context_;
    bool lastIsPunc_ = false;

    PinyinMode mode_ = PinyinMode::Normal;

    // Stroke filter
    std::shared_ptr<CandidateList> strokeCandidateList_;
    InputBuffer strokeBuffer_;

    // Forget candidate
    std::shared_ptr<CandidateList> forgetCandidateList_;

    std::unique_ptr<EventSourceTime> cancelLastEvent_;

    std::vector<std::string> predictWords_;

    int keyReleased_ = -1;
    int keyReleasedIndex_ = -2;
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

class StrokeFilterCandidateWord : public CandidateWord {
public:
    StrokeFilterCandidateWord(PinyinEngine *engine, Text text, int index)
        : CandidateWord(), engine_(engine), index_(index) {
        setText(std::move(text));
    }

    void select(InputContext *inputContext) const override {
        auto state = inputContext->propertyFor(&engine_->factory());
        if (!state->strokeCandidateList_ ||
            state->strokeCandidateList_->toBulk()->totalSize() <= index_) {
            FCITX_ERROR() << "Stroke candidate is not consistent. Probably a "
                             "bug in implementation";
            return;
        }
        // Forward the selection to internal candidate list.
        state->strokeCandidateList_->toBulk()->candidateFromAll(index_).select(
            inputContext);
        engine_->resetStroke(inputContext);
    }

private:
    PinyinEngine *engine_;
    int index_;
};

class ForgetCandidateWord : public CandidateWord {
public:
    ForgetCandidateWord(PinyinEngine *engine, Text text, size_t index)
        : CandidateWord(), engine_(engine), index_(index) {
        setText(std::move(text));
    }

    void select(InputContext *inputContext) const override {
        auto state = inputContext->propertyFor(&engine_->factory());
        if (state->mode_ != PinyinMode::ForgetCandidate) {
            FCITX_ERROR() << "Candidate is not consistent. Probably a "
                             "bug in implementation";
            return;
        }

        if (index_ < state->context_.candidates().size()) {
            auto &sentence = state->context_.candidates()[index_];
            auto py = state->context_.candidateFullPinyin(index_);
            state->context_.ime()->dict()->removeWord(
                libime::PinyinDictionary::UserDict, py, sentence.toString());
            state->context_.ime()->model()->history().forget(
                sentence.toString());
        }
        engine_->resetForgetCandidate(inputContext);
        engine_->doReset(inputContext);
    }

private:
    PinyinEngine *engine_;
    size_t index_;
};

class ExtraCandidateWord : public CandidateWord {
public:
    ExtraCandidateWord(PinyinEngine *engine, std::string word)
        : CandidateWord(), engine_(engine), word_(std::move(word)) {
        setText(Text(word_));
    }

    void select(InputContext *inputContext) const override {
        inputContext->commitString(word_);
        engine_->doReset(inputContext);
    }

private:
    PinyinEngine *engine_;
    std::string word_;
};

class SpellCandidateWord : public CandidateWord {
public:
    SpellCandidateWord(PinyinEngine *engine, std::string word)
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
    if (candidateList->size()) {
        candidateList->setGlobalCursorIndex(0);
    }
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
                weight += fullWeight / 2;
            } else {
                weight += invalidWeight;
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
    return (weight + 7) / 10;
}

bool isStroke(const std::string &input) {
    static const std::unordered_set<char> py{'h', 'p', 's', 'z', 'n'};
    return std::all_of(input.begin(), input.end(),
                       [](char c) { return py.count(c); });
}

#ifdef FCITX_HAS_LUA
std::vector<std::string>
PinyinEngine::luaCandidateTrigger(InputContext *ic,
                                  const std::string &candidateString) {
    std::vector<std::string> result;
    RawConfig arg;
    arg.setValue(candidateString);
    auto ret = imeapi()->call<ILuaAddon::invokeLuaFunction>(
        ic, "candidateTrigger", arg);
    auto length = ret.valueByPath("Length");
    try {
        if (length) {
            auto n = std::stoi(*length);
            for (int i = 0; i < n; i++) {
                auto *candidate = ret.valueByPath(std::to_string(i));
                if (candidate && !candidate->empty()) {
                    result.push_back(*candidate);
                }
            }
        }
    } catch (...) {
    }
    return result;
}
#endif

bool PinyinEngine::showClientPreedit(InputContext *inputContext) const {
    return config_.showPreeditInApplication.value() &&
           inputContext->capabilityFlags().test(CapabilityFlag::Preedit);
}

Text PinyinEngine::fetchAndSetClientPreedit(
    InputContext *inputContext, const libime::PinyinContext &context) const {
    auto preeditWithCursor = context.preeditWithCursor();
    Text preedit(std::move(preeditWithCursor.first));
    preedit.setCursor(preeditWithCursor.second);
    auto &inputPanel = inputContext->inputPanel();
    if (showClientPreedit(inputContext)) {
        inputPanel.setClientPreedit(preedit);
    } else {
        inputPanel.setClientPreedit(
            Text(context.sentence(), TextFormatFlag::Underline));
    }
    return preedit;
}

void PinyinEngine::updateUI(InputContext *inputContext) {
    inputContext->inputPanel().reset();

    auto state = inputContext->propertyFor(&factory_);
    // Use const ref to avoid accidentally change anything.
    const auto &context = state->context_;
    if (context.selected()) {
        auto sentence = context.sentence();
        if (!inputContext->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive)) {
            state->context_.learn();
        }
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        inputContext->commitString(sentence);
        if (*config_.predictionEnabled) {
            initPredict(inputContext);
        }
        state->context_.clear();
        return;
    }

    do {
        if (context.userInput().empty()) {
            break;
        }
        // Update Preedit.
        auto &inputPanel = inputContext->inputPanel();
        Text preedit = fetchAndSetClientPreedit(inputContext, state->context_);
        if (!showClientPreedit(inputContext)) {
            inputPanel.setPreedit(preedit);
        }
        // Update candidate
        auto &candidates = context.candidates();
        if (candidates.empty()) {
            break;
        }

        // Setup candidate list.
        auto candidateList = std::make_unique<CommonCandidateList>();
        candidateList->setPageSize(*config_.pageSize);
        candidateList->setCursorPositionAfterPaging(
            CursorPositionAfterPaging::ResetToFirst);

        // Precompute some values.
        const auto selectedLength = context.selectedLength();
        const auto selectedSentence = context.selectedSentence();

        /// Create cloud candidate. {{{
        std::unique_ptr<CloudPinyinCandidateWord> cloud;
        if (*config_.cloudPinyinEnabled && cloudpinyin() &&
            !inputContext->capabilityFlags().testAny(
                CapabilityFlag::PasswordOrSensitive)) {
            using namespace std::placeholders;
            auto fullPinyin = context.useShuangpin()
                                  ? context.candidateFullPinyin(0)
                                  : context.userInput().substr(selectedLength);
            cloud = std::make_unique<CloudPinyinCandidateWord>(
                cloudpinyin(), fullPinyin, selectedSentence, inputContext,
                std::bind(&PinyinEngine::cloudPinyinSelected, this, _1, _2,
                          _3));
        }
        /// }}}

        /// Create spell candidate {{{
        int engNess;
        auto parsedPy = preedit.stringAt(0).substr(selectedSentence.size());
        std::vector<std::unique_ptr<SpellCandidateWord>> spellCands;
        if (spell() &&
            (engNess = englishNess(parsedPy, context.useShuangpin()))) {
            auto py = context.userInput().substr(selectedLength);
            auto results = spell()->call<ISpell::hintWithProvider>(
                "en", SpellProvider::Custom, py, engNess);
            spellCands.reserve(results.size());
            for (auto &result : results) {
                if (cloud && cloud->filled() && cloud->word() == result) {
                    cloud.reset();
                }
                spellCands.push_back(
                    std::make_unique<SpellCandidateWord>(this, result));
            }
        }
        /// }}}

        /// Create stroke candidate {{{
        std::vector<std::unique_ptr<StrokeCandidateWord>> strokeCands;
        if (pinyinhelper() && context.selectedLength() == 0 &&
            isStroke(context.userInput())) {
            int limit = (context.userInput().size() + 4) / 5;
            if (limit > 3) {
                limit = 3;
            }
            auto results = pinyinhelper()->call<IPinyinHelper::lookupStroke>(
                context.userInput(), limit);
            for (auto &result : results) {
                utf8::getChar(result.first);
                auto py = pinyinhelper()->call<IPinyinHelper::lookup>(
                    utf8::getChar(result.first));
                auto pystr = stringutils::join(py, " ");
                strokeCands.push_back(std::make_unique<StrokeCandidateWord>(
                    this, result.first, pystr));
            }
        }
        /// }}}

        size_t idx = 0;
        for (const auto &candidate : candidates) {
            auto candidateString = candidate.toString();
            std::vector<std::string> extraCandidates;
#ifdef FCITX_HAS_LUA
            // To invoke lua trigger, we need "raw full sentence". Also, check
            // against nbest, otherwise single char may be invoked for too much
            // times.
            if (selectedLength == 0 &&
                static_cast<int>(idx) <
                    std::max(*config_.nbest, *config_.pageSize) &&
                imeapi() &&
                candidate.sentence().back()->to()->index() ==
                    context.userInput().size()) {
                extraCandidates =
                    luaCandidateTrigger(inputContext, candidateString);
            }
#endif
            if (cloud && cloud->filled() && cloud->word() == candidateString) {
                cloud.reset();
            }
            candidateList->append<PinyinCandidateWord>(
                this, Text(std::move(candidateString)), idx);
            for (auto &extraCandidate : extraCandidates) {
                candidateList->append<ExtraCandidateWord>(this, extraCandidate);
            }
            idx++;
            // We don't want to do too much comparision for cloud pinyin.
            if (cloud && (!cloud->filled() || !cloud->word().empty()) &&
                (static_cast<int>(idx) >
                     std::max(*config_.nbest, *config_.cloudPinyinIndex - 1) ||
                 idx == candidates.size())) {
                auto desiredPos = *config_.cloudPinyinIndex - 1;
                if (desiredPos > candidateList->totalSize()) {
                    desiredPos = candidateList->totalSize();
                }
                candidateList->insert(desiredPos, std::move(cloud));
            }
            if (!spellCands.empty() && (idx == 1 || idx == candidates.size())) {
                for (auto &spellCand : spellCands) {
                    candidateList->append(std::move(spellCand));
                }
                spellCands.clear();
            }
            if (!strokeCands.empty() &&
                (candidateList->totalSize() + 1 >= *config_.pageSize ||
                 idx == candidates.size())) {
                int desiredPos =
                    *config_.pageSize - static_cast<int>(strokeCands.size());
                if (desiredPos < 0 || desiredPos > candidateList->totalSize()) {
                    desiredPos = candidateList->totalSize();
                }
                for (auto &strokeCand : strokeCands) {
                    candidateList->insert(desiredPos, std::move(strokeCand));
                    desiredPos += 1;
                }
                strokeCands.clear();
            }
        }
        candidateList->setSelectionKey(selectionKeys_);
        if (candidateList->size()) {
            candidateList->setGlobalCursorIndex(0);
        }
        inputPanel.setCandidateList(std::move(candidateList));
    } while (0);
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
    auto disableFiles = standardPath.multiOpen(StandardPath::Type::PkgData,
                                               "pinyin/dictionaries", O_RDONLY,
                                               filter::Suffix(".dict.disable"));
    ime_->dict()->removeAll();
    if (*config_.emojiEnabled) {
        auto file = standardPath.open(StandardPath::Type::PkgData,
                                      "pinyin/emoji.dict", O_RDONLY);
        loadDict(file);
    }
    if (*config_.chaiziEnabled) {
        auto file = standardPath.open(StandardPath::Type::PkgData,
                                      "pinyin/chaizi.dict", O_RDONLY);
        loadDict(file);
        ime_->dict()->setFlags(ime_->dict()->dictSize() - 1,
                               libime::PinyinDictFlag::FullMatch);
    }
    for (const auto &file : files) {
        if (disableFiles.count(stringutils::concat(file.first, ".disable"))) {
            PINYIN_DEBUG() << "Dictionary: " << file.first << " is disabled.";
            continue;
        }
        PINYIN_DEBUG() << "Loading extra dictionary: " << file.first;
        loadDict(file.second);
    }
}

void PinyinEngine::reloadConfig() {
    PINYIN_DEBUG() << "Reload pinyin config.";
    readAsIni(config_, "conf/pinyin.conf");
    ime_->setNBest(*config_.nbest);
    ime_->setPartialLongWordLimit(*config_.longWordLimit);
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

bool PinyinEngine::handleCloudpinyinTrigger(KeyEvent &event) {
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
        return true;
    }
    return false;
}

bool PinyinEngine::handle2nd3rdSelection(KeyEvent &event) {
    auto inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList) {
        return false;
    }

    struct {
        const KeyList &list;
        int selection;
    } keyHandlers[] = {
        // Index starts with 0
        {*config_.secondCandidate, 1},
        {*config_.thirdCandidate, 2},
    };

    auto state = inputContext->propertyFor(&factory_);
    int keyReleased = state->keyReleased_;
    int keyReleasedIndex = state->keyReleasedIndex_;
    // Keep these two values, and reset them in the state
    state->keyReleased_ = -1;
    state->keyReleasedIndex_ = -2;
    const bool isModifier = event.origKey().isModifier();
    if (event.isRelease()) {
        int idx = 0;
        for (auto &keyHandler : keyHandlers) {
            if (keyReleased == idx &&
                keyReleasedIndex ==
                    event.origKey().keyListIndex(keyHandler.list)) {
                if (isModifier) {
                    if (keyHandler.selection < candidateList->size()) {
                        candidateList->candidate(keyHandler.selection)
                            .select(inputContext);
                    }
                    event.filterAndAccept();
                    return true;
                } else {
                    event.filter();
                    return true;
                }
            }
            idx++;
        }
    }

    if (!event.filtered() && !event.isRelease()) {
        int idx = 0;
        for (auto &keyHandler : keyHandlers) {
            auto keyIdx = event.origKey().keyListIndex(keyHandler.list);
            if (keyIdx >= 0) {
                state->keyReleased_ = idx;
                state->keyReleasedIndex_ = keyIdx;
                if (isModifier) {
                    // don't forward to input method, but make it pass
                    // through to client.
                    event.filter();
                    return true;
                } else {
                    if (keyHandler.selection < candidateList->size()) {
                        candidateList->candidate(keyHandler.selection)
                            .select(inputContext);
                    }
                    event.filterAndAccept();
                    return true;
                }
            }
            idx++;
        }
    }
    return false;
}

bool PinyinEngine::handleCandidateList(KeyEvent &event) {
    auto inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    if (!candidateList) {
        return false;
    }
    int idx = event.key().keyListIndex(selectionKeys_);
    if (idx >= 0) {
        event.filterAndAccept();
        if (idx < candidateList->size()) {
            candidateList->candidate(idx).select(inputContext);
        }
        return true;
    }

    if (event.key().checkKeyList(*config_.prevPage)) {
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

    if (event.key().checkKeyList(*config_.nextPage)) {
        event.filterAndAccept();
        candidateList->toPageable()->next();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        return true;
    }

    if (auto movable = candidateList->toCursorMovable()) {
        if (event.key().checkKeyList(*config_.nextCandidate)) {
            movable->nextCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        } else if (event.key().checkKeyList(*config_.prevCandidate)) {
            movable->prevCandidate();
            inputContext->updateUserInterface(
                UserInterfaceComponent::InputPanel);
            event.filterAndAccept();
            return true;
        }
    }
    return false;
}

void PinyinEngine::updateStroke(InputContext *inputContext) {
    auto state = inputContext->propertyFor(&factory_);
    auto &inputPanel = inputContext->inputPanel();
    inputPanel.reset();

    const auto preeditWithCursor = state->context_.preeditWithCursor();
    Text preedit = fetchAndSetClientPreedit(inputContext, state->context_);
    preedit.append(_("\t[Stroke Filtering] "));
    preedit.append(pinyinhelper()->call<IPinyinHelper::prettyStrokeString>(
        state->strokeBuffer_.userInput()));
    inputPanel.setPreedit(preedit);

    auto candidateList = std::make_unique<CommonCandidateList>();
    candidateList->setPageSize(*config_.pageSize);
    candidateList->setCursorPositionAfterPaging(
        CursorPositionAfterPaging::ResetToFirst);

    auto origCandidateList = state->strokeCandidateList_->toBulk();
    for (int i = 0; i < origCandidateList->totalSize(); i++) {
        auto &candidate = origCandidateList->candidateFromAll(i);
        auto str = candidate.text().toStringForCommit();
        if (auto length = utf8::lengthValidated(str);
            length != utf8::INVALID_LENGTH && length >= 1) {
            auto charEnd = utf8::nextChar(str.begin());
            std::string chr(str.begin(), charEnd);
            std::string stroke =
                pinyinhelper()->call<IPinyinHelper::reverseLookupStroke>(chr);
            if (stringutils::startsWith(stroke,
                                        state->strokeBuffer_.userInput())) {
                candidateList->append<StrokeFilterCandidateWord>(
                    this, candidate.text(), i);
            }
        }
    }
    candidateList->setSelectionKey(selectionKeys_);
    if (candidateList->size()) {
        candidateList->setGlobalCursorIndex(0);
    }
    inputContext->inputPanel().setCandidateList(std::move(candidateList));
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::updateForgetCandidate(InputContext *inputContext) {
    auto state = inputContext->propertyFor(&factory_);
    auto &inputPanel = inputContext->inputPanel();
    inputPanel.reset();

    fetchAndSetClientPreedit(inputContext, state->context_);
    Text aux(_("[Select the word to remove from history]"));
    inputPanel.setAuxUp(aux);

    auto candidateList = std::make_unique<CommonCandidateList>();
    candidateList->setPageSize(*config_.pageSize);
    candidateList->setCursorPositionAfterPaging(
        CursorPositionAfterPaging::ResetToFirst);

    auto origCandidateList = state->forgetCandidateList_->toBulk();
    for (int i = 0; i < origCandidateList->totalSize(); i++) {
        auto &candidate = origCandidateList->candidateFromAll(i);
        if (auto pyCandidate =
                dynamic_cast<const PinyinCandidateWord *>(&candidate)) {
            if (pyCandidate->idx_ >= state->context_.candidates().size() ||
                state->context_.candidates()[pyCandidate->idx_]
                        .sentence()
                        .size() != 1 ||
                state->context_.candidateFullPinyin(pyCandidate->idx_)
                    .empty()) {
                continue;
            }

            candidateList->append<ForgetCandidateWord>(
                this, pyCandidate->text(), pyCandidate->idx_);
        }
    }
    candidateList->setSelectionKey(selectionKeys_);
    if (candidateList->size()) {
        candidateList->setGlobalCursorIndex(0);
    }
    inputContext->inputPanel().setCandidateList(std::move(candidateList));
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void PinyinEngine::resetStroke(InputContext *inputContext) {
    auto state = inputContext->propertyFor(&factory_);
    state->strokeCandidateList_.reset();
    state->strokeBuffer_.clear();
    if (state->mode_ == PinyinMode::StrokeFilter) {
        state->mode_ = PinyinMode::Normal;
    }
}

void PinyinEngine::resetForgetCandidate(InputContext *inputContext) {
    auto state = inputContext->propertyFor(&factory_);
    state->forgetCandidateList_.reset();
    if (state->mode_ == PinyinMode::ForgetCandidate) {
        state->mode_ = PinyinMode::Normal;
    }
}

bool PinyinEngine::handleStrokeFilter(KeyEvent &event) {
    auto inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    auto state = inputContext->propertyFor(&factory_);
    if (state->mode_ == PinyinMode::Normal) {
        if (candidateList && candidateList->size() && candidateList->toBulk() &&
            event.key().checkKeyList(*config_.selectByStroke) &&
            pinyinhelper()) {
            resetStroke(inputContext);
            state->strokeCandidateList_ = candidateList;
            state->mode_ = PinyinMode::StrokeFilter;
            updateStroke(inputContext);
            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (state->mode_ != PinyinMode::StrokeFilter) {
        return false;
    }

    event.filterAndAccept();
    // Skip all key combinition.
    if (event.key().states().testAny(KeyState::SimpleMask)) {
        return true;
    }

    if (event.key().check(FcitxKey_Escape)) {
        resetStroke(inputContext);
        updateUI(inputContext);
        return true;
    } else if (event.key().check(FcitxKey_BackSpace)) {
        state->strokeBuffer_.backspace();
        updateStroke(inputContext);
        return true;
    }
    // if it gonna commit something
    auto c = Key::keySymToUnicode(event.key().sym());
    if (!c) {
        return true;
    }

    if (event.key().check(FcitxKey_h) || event.key().check(FcitxKey_p) ||
        event.key().check(FcitxKey_s) || event.key().check(FcitxKey_n) ||
        event.key().check(FcitxKey_z)) {
        static const std::unordered_map<FcitxKeySym, char> strokeMap{
            {FcitxKey_h, '1'},
            {FcitxKey_s, '2'},
            {FcitxKey_p, '3'},
            {FcitxKey_n, '4'},
            {FcitxKey_z, '5'}};
        if (auto iter = strokeMap.find(event.key().sym());
            iter != strokeMap.end()) {
            state->strokeBuffer_.type(iter->second);
            updateStroke(inputContext);
        }
    }

    return true;
}

bool PinyinEngine::handleForgetCandidate(KeyEvent &event) {
    auto inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    auto state = inputContext->propertyFor(&factory_);
    if (state->mode_ == PinyinMode::Normal) {
        if (candidateList && candidateList->size() && candidateList->toBulk() &&
            event.key().checkKeyList(*config_.forgetWord)) {
            resetForgetCandidate(inputContext);
            state->forgetCandidateList_ = candidateList;
            state->mode_ = PinyinMode::ForgetCandidate;
            updateForgetCandidate(inputContext);
            event.filterAndAccept();
            return true;
        }
        return false;
    }

    if (state->mode_ != PinyinMode::ForgetCandidate) {
        return false;
    }

    event.filterAndAccept();
    // Skip all key combinition.
    if (event.key().states().testAny(KeyState::SimpleMask)) {
        return true;
    }

    if (event.key().check(FcitxKey_Escape)) {
        resetForgetCandidate(inputContext);
        updateUI(inputContext);
        return true;
    }
    return true;
}

bool PinyinEngine::handlePunc(KeyEvent &event) {
    auto inputContext = event.inputContext();
    auto candidateList = inputContext->inputPanel().candidateList();
    auto state = inputContext->propertyFor(&factory_);
    if (event.filtered()) {
        return false;
    }
    if (event.key().states().testAny(KeyState::SimpleMask)) {
        return false;
    }
    // if it gonna commit something
    auto c = Key::keySymToUnicode(event.key().sym());
    if (!c) {
        return false;
    }
    if (candidateList && candidateList->size()) {
        candidateList->candidate(0).select(inputContext);
    }
    std::string punc;
    // skip key pad
    if (!event.key().isKeyPad()) {
        punc = punctuation()->call<IPunctuation::pushPunctuation>(
            "zh_CN", inputContext, c);
    }
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
                    boost::format(_("Press %1% for %2% and %3% for %4%")) %
                    keyString % output % _("Return") % altOutput);
            } else {
                text = boost::str(boost::format(_("Press %1% for %2%")) %
                                  keyString % altOutput);
            }
        }
        quickphrase()->call<IQuickPhrase::trigger>(
            inputContext, text, "", output, altOutput, Key(FcitxKey_semicolon));
        event.filterAndAccept();
        return true;
    }

    if (punc.size()) {
        event.filterAndAccept();
        inputContext->commitString(punc);
    }
    if (inputContext->capabilityFlags().test(
            CapabilityFlag::KeyEventOrderFix) &&
        !event.filtered()) {
        // Re-forward the event to ensure we got delivered later than
        // commit.
        event.filterAndAccept();
        inputContext->forwardKey(event.rawKey(), event.isRelease(),
                                 event.time());
    }
    state->lastIsPunc_ = true;
    return false;
}

void PinyinEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &event) {
    FCITX_UNUSED(entry);
    PINYIN_DEBUG() << "Pinyin receive key: " << event.key() << " "
                   << event.isRelease();
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);

    // 2nd/3rd selection is allowed to be modifier only, handle them before we
    // skip the release.
    if (handle2nd3rdSelection(event)) {
        return;
    }

    // by pass all key release
    if (event.isRelease()) {
        return;
    }

    // and by pass all modifier
    if (event.key().isModifier()) {
        return;
    }

    if (handleCloudpinyinTrigger(event)) {
        return;
    }

    auto candidateList = inputContext->inputPanel().candidateList();
    bool lastIsPunc = state->lastIsPunc_;
    state->lastIsPunc_ = false;

    // handle number key selection and prev/next page/candidate.
    if (handleCandidateList(event)) {
        return;
    }

    if (handleStrokeFilter(event)) {
        return;
    }

    if (handleForgetCandidate(event)) {
        return;
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

    if (event.key().isLAZ() || event.key().isUAZ() ||
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
        } else if (event.key().sym() == FcitxKey_Return) {
            inputContext->commitString(state->context_.userInput());
            state->context_.clear();
            event.filterAndAccept();
        } else if (event.key().check(FcitxKey_space)) {
            if (candidateList && candidateList->size()) {
                event.filterAndAccept();
                int idx = candidateList->cursorIndex();
                if (idx < 0) {
                    idx = 0;
                }
                candidateList->candidate(idx).select(inputContext);
                return;
            }
        } else if (int idx =
                       event.key().keyListIndex(*config_.selectCharFromPhrase);
                   idx >= 0) {
            if (candidateList && candidateList->size() &&
                candidateList->cursorIndex() >= 0) {
                auto str =
                    candidateList->candidate(candidateList->cursorIndex())
                        .text()
                        .toStringForCommit();
                // Validate string and length.
                if (auto len = utf8::lengthValidated(str);
                    len != utf8::INVALID_LENGTH &&
                    len > static_cast<size_t>(idx)) {
                    // Get idx-th char.
                    auto charRange =
                        std::next(utf8::MakeUTF8CharRange(str).begin(), idx)
                            .charRange();
                    std::string chr(charRange.first, charRange.second);
                    inputContext->commitString(chr);
                    event.filterAndAccept();
                    state->context_.clear();
                }
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
    if (handlePunc(event)) {
        return;
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
    resetStroke(inputContext);
    resetForgetCandidate(inputContext);
    state->mode_ = PinyinMode::Normal;
    state->context_.clear();
    state->predictWords_.clear();
    inputContext->inputPanel().reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    // state->lastIsPunc_ = false;

    state->keyReleased_ = -1;
    state->keyReleasedIndex_ = -2;
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
                if (state->context_.useShuangpin()) {
                    bool end = false;
                    for (auto &sppinyin :
                         MakeIterRange(pinyinsIter, pinyinsEnd)) {
                        sppinyin = libime::PinyinEncoder::shuangpinToPinyin(
                            sppinyin, *ime_->shuangpinProfile());
                        if (sppinyin.empty()) {
                            end = true;
                            break;
                        }
                    }
                    if (end) {
                        break;
                    }
                }
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
