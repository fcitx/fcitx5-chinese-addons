/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYIN_PINYIN_H_
#define _PINYIN_PINYIN_H_

#include "customphrase.h"
#include "symboldictionary.h"
#include "workerthread.h"
#include <cstddef>
#include <cstdint>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/inputbuffer.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx-utils/unixfd.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <filesystem>
#include <future>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyinime.h>
#include <libime/pinyin/pinyinprediction.h>
#include <list>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcitx {

template <typename Base = NoAnnotation>
struct OptionalHideInDescriptionBase : public Base {
    void setHidden(bool hidden) { hidden_ = hidden; }

    bool skipDescription() { return hidden_; }
    using Base::dumpDescription;
    using Base::skipSave;

private:
    bool hidden_ = false;
};

using OptionalHideInDescription = OptionalHideInDescriptionBase<>;

class OptionalHiddenSubConfigOption : public SubConfigOption {
public:
    using SubConfigOption::SubConfigOption;

    void setHidden(bool hidden) { hidden_ = hidden; }

    bool skipDescription() const override { return hidden_; }

private:
    bool hidden_ = false;
};

enum class SwitchInputMethodBehavior { Clear, CommitPreedit, CommitDefault };

FCITX_CONFIG_ENUM_NAME_WITH_I18N(SwitchInputMethodBehavior, N_("Clear"),
                                 N_("Commit current preedit"),
                                 N_("Commit default selection"))

FCITX_CONFIG_ENUM(ShuangpinProfileEnum, Ziranma, MS, Ziguang, ABC,
                  Zhongwenzhixing, PinyinJiajia, Xiaohe, Custom)

FCITX_CONFIG_ENUM_I18N_ANNOTATION(ShuangpinProfileEnum, N_("Ziranma"), N_("MS"),
                                  N_("Ziguang"), N_("ABC"),
                                  N_("Zhongwenzhixing"), N_("PinyinJiajia"),
                                  N_("Xiaohe"), N_("Custom"))

enum class PreeditMode { No, ComposingPinyin, CommitPreview };

FCITX_CONFIG_ENUM_NAME_WITH_I18N(PreeditMode, N_("Do not show"),
                                 N_("Composing pinyin"), N_("Commit preview"))

enum class BackspaceBehaviorOnPrediction {
    OnlyClearCandidates,
    ClearCandidatesAndBackspace,
    BackspaceWhenNotUsingOnScreenKeyboard
};

FCITX_CONFIG_ENUM_NAME_WITH_I18N(
    BackspaceBehaviorOnPrediction, N_("Only Clear candidates"),
    N_("Clear candidates & backspace"),
    N_("Backspace when not using on-screen keyboard"))

enum class CorrectionLayout {
    None,
    Qwerty,
};

FCITX_CONFIG_ENUM_NAME_WITH_I18N(CorrectionLayout, N_("None"), N_("QWERTY"))

FCITX_CONFIGURATION(
    FuzzyConfig, Option<bool> ue{this, "VE_UE", _("ue -> ve"), true};
    Option<bool> commonTypo{this, "NG_GN", _("Common Typo"), true};
    Option<bool> inner{this, "Inner", _("Inner Segment (xian -> xi'an)"), true};
    Option<bool> innerShort{this, "InnerShort",
                            _("Inner Segment for Short Pinyin (qie -> qi'e)"),
                            true};
    Option<bool> partialFinal{this, "PartialFinal",
                              _("Match partial finals (e -> en, eng, ei)"),
                              true};
    OptionWithAnnotation<bool, OptionalHideInDescription> partialSp{
        this, "PartialSp",
        _("Match partial shuangpin if input length is longer than 4"), false};
    Option<bool> v{this, "V_U", _("u <-> v"), false};
    Option<bool> an{this, "AN_ANG", _("an <-> ang"), false};
    Option<bool> en{this, "EN_ENG", _("en <-> eng"), false};
    Option<bool> ian{this, "IAN_IANG", _("ian <-> iang"), false};
    Option<bool> in{this, "IN_ING", _("in <-> ing"), false};
    Option<bool> ou{this, "U_OU", _("u <-> ou"), false};
    Option<bool> uan{this, "UAN_UANG", _("uan <-> uang"), false};
    Option<bool> c{this, "C_CH", _("c <-> ch"), false};
    Option<bool> f{this, "F_H", _("f <-> h"), false};
    Option<bool> l{this, "L_N", _("l <-> n"), false};
    Option<bool> r{this, "L_R", _("l <-> r"), false};
    Option<bool> s{this, "S_SH", _("s <-> sh"), false};
    Option<bool> z{this, "Z_ZH", _("z <-> zh"), false};
    OptionWithAnnotation<CorrectionLayout, CorrectionLayoutI18NAnnotation>
        correction{this, "Correction", _("Correction Layout"),
                   isAndroid() ? CorrectionLayout::Qwerty
                               : CorrectionLayout::None};)

FCITX_CONFIGURATION(
    PinyinEngineConfig,
    OptionWithAnnotation<
        ShuangpinProfileEnum,
        OptionalHideInDescriptionBase<ShuangpinProfileEnumI18NAnnotation>>
        shuangpinProfile{this, "ShuangpinProfile", _("Shuangpin Profile"),
                         ShuangpinProfileEnum::Ziranma};
    OptionWithAnnotation<bool, OptionalHideInDescription> showShuangpinMode{
        this, "ShowShuangpinMode", _("Show current shuangpin mode"), true};
    Option<int, IntConstrain> pageSize{
        this, "PageSize", _("Candidates Per Page"), 7, IntConstrain(3, 10)};
    Option<bool> spellEnabled{this, "SpellEnabled",
                              _("Show English Candidates"), true};
    Option<bool> symbolsEnabled{this, "SymbolsEnabled",
                                _("Show symbol candidates"), true};
    Option<bool> chaiziEnabled{this, "ChaiziEnabled",
                               _("Show Chaizi candidates"), true};
    Option<bool> extBEnabled{
        this, "ExtBEnabled",
        _("Enable more Characters after Unicode CJK Extension B"),
        !isAndroid()};
    Option<bool> strokeCandidateEnabled{
        this, "StrokeCandidateEnabled",
        _("Show stroke candidates when typing with h(一), s(丨), p(丿), n(㇏), "
          "z(𠃍)"),
        true};
    OptionWithAnnotation<bool, OptionalHideInDescription> cloudPinyinEnabled{
        this, "CloudPinyinEnabled", _("Enable Cloud Pinyin"), false};
    OptionalHiddenSubConfigOption cloudpinyin{
        this, "CloudPinyin", _("Configure Cloud Pinyin"),
        "fcitx://config/addon/cloudpinyin"};
    Option<int, IntConstrain, DefaultMarshaller<int>, OptionalHideInDescription>
        cloudPinyinIndex{this, "CloudPinyinIndex",
                         _("Cloud Pinyin Candidate Order"), 2,
                         IntConstrain(1, 10)};
    OptionWithAnnotation<bool, OptionalHideInDescription> cloudPinyinAnimation{
        this, "CloudPinyinAnimation",
        _("Show animation when Cloud Pinyin is loading"), true};
    OptionWithAnnotation<bool, OptionalHideInDescription>
        keepCloudPinyinPlaceHolder{this, "KeepCloudPinyinPlaceHolder",
                                   _("Always show Cloud Pinyin place holder"),
                                   false};
    OptionWithAnnotation<PreeditMode, PreeditModeI18NAnnotation> preeditMode{
        this, "PreeditMode", _("Preedit Mode"),
        isAndroid() ? PreeditMode::No : PreeditMode::ComposingPinyin};
    Option<bool> preeditCursorPositionAtBeginning{
        this, "PreeditCursorPositionAtBeginning",
        _("Fix embedded preedit cursor at the beginning of the preedit"),
        !isAndroid() && !isApple() && !isEmscripten()};
    Option<bool> showActualPinyinInPreedit{
        this, "PinyinInPreedit", _("Show complete pinyin in preedit"), false};
    Option<bool> predictionEnabled{this, "Prediction", _("Enable Prediction"),
                                   isAndroid()};
    Option<int, IntConstrain> predictionSize{this, "PredictionSize",
                                             _("Number of Predictions"), 49,
                                             IntConstrain(3, 100)};
    OptionWithAnnotation<BackspaceBehaviorOnPrediction,
                         BackspaceBehaviorOnPredictionI18NAnnotation>
        backspaceBehaviorOnPrediction{
            this, "BackspaceBehaviorOnPrediction",
            _("Backspace behavior on prediction"),
            BackspaceBehaviorOnPrediction::
                BackspaceWhenNotUsingOnScreenKeyboard};
    OptionWithAnnotation<SwitchInputMethodBehavior,
                         SwitchInputMethodBehaviorI18NAnnotation>
        switchInputMethodBehavior{this, "SwitchInputMethodBehavior",
                                  _("Action when switching input method"),
                                  SwitchInputMethodBehavior::CommitPreedit};
    KeyListOption forgetWord{this,
                             "ForgetWord",
                             _("Forget word"),
                             {Key("Control+7")},
                             KeyListConstrain()};
    KeyListOption prevPage{
        this,
        "PrevPage",
        _("Previous Page"),
        {Key(FcitxKey_minus), Key(FcitxKey_Up), Key(FcitxKey_KP_Up),
         Key(FcitxKey_Page_Up)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextPage{
        this,
        "NextPage",
        _("Next Page"),
        {Key(FcitxKey_equal), Key(FcitxKey_Down), Key(FcitxKey_KP_Down),
         Key(FcitxKey_Page_Down)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption prevCandidate{
        this,
        "PrevCandidate",
        _("Previous Candidate"),
        {Key("Shift+Tab")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextCandidate{
        this,
        "NextCandidate",
        _("Next Candidate"),
        {Key("Tab")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption currentCandidate{
        this,
        "CurrentCandidate",
        _("Select Current Candidate"),
        {Key(FcitxKey_space), Key(FcitxKey_KP_Space)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption commitRawInput{
        this,
        "CommitRawInput",
        _("Commit Raw Input"),
        {Key("Return"), Key("KP_Enter"), Key("Control+Return"),
         Key("Control+KP_Enter"), Key("Shift+Return"), Key("Shift+KP_Enter"),
         Key("Control+Shift+Return"), Key("Control+Shift+KP_Enter")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption secondCandidate{
        this,
        "SecondCandidate",
        _("Select Second Candidate"),
        {},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess,
                          KeyConstrainFlag::AllowModifierOnly})};
    KeyListOption thirdCandidate{
        this,
        "ThirdCandidate",
        _("Select Third Candidate"),
        {},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess,
                          KeyConstrainFlag::AllowModifierOnly})};
    Option<bool> useKeypadAsSelectionKey{
        this, "UseKeypadAsSelection", _("Use Keypad as Selection key"), false};
    KeyListOption selectCharFromPhrase{
        this,
        "ChooseCharFromPhrase",
        _("Choose Character from Phrase"),
        {Key("["), Key("]")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    Option<bool> useBackSpaceToUnselect{
        this, "BackSpaceToUnselect", _("Use BackSpace to cancel the selection"),
        true};
    KeyListOption selectByStroke{
        this,
        "FilterByStroke",
        _("Filter by stroke"),
        {Key("grave")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    Option<int, IntConstrain> nbest{this, "Number of sentence",
                                    _("Number of Sentences"), 2,
                                    IntConstrain(1, 3)};
    Option<int, IntConstrain, DefaultMarshaller<int>, ToolTipAnnotation>
        wordCandidateLimit{this,
                           "WordCandidateLimit",
                           _("Number of Phrase Candidates"),
                           15,
                           IntConstrain(0),
                           {},
                           {_("Set to 0 will show all candidates.")}};
    Option<int, IntConstrain> longWordLimit{
        this, "LongWordLengthLimit",
        _("Prompt long word length when input length over (0 for disable)"), 4,
        IntConstrain(0, 10)};
    ExternalOption dictmanager{this, "DictManager", _("Manage Dictionaries"),
                               "fcitx://config/addon/pinyin/dictmanager"};
    ExternalOption customphrase{this, "CustomPhrase", _("Manage Custom Phrase"),
                                "fcitx://config/addon/pinyin/customphrase"};
    SubConfigOption punctuationMap{
        this, "Punctuation", _("Punctuation"),
        "fcitx://config/addon/punctuation/punctuationmap/zh_CN"};
    SubConfigOption chttrans{
        this, "Chttrans",
        _("Configure Simplified/Traditional Chinese Conversion"),
        "fcitx://config/addon/chttrans"};
    Option<Key, KeyConstrain> quickphraseKey{
        this,
        "QuickPhraseKey",
        _("Key to trigger quickphrase"),
        Key{FcitxKey_semicolon},
        {KeyConstrainFlag::AllowModifierLess}};
    Option<bool> useVAsQuickphrase{this, "VAsQuickphrase",
                                   _("Use V to trigger quickphrase"), true};
    ExternalOption quickphrase{this, "QuickPhrase", _("Quick Phrase"),
                               "fcitx://config/addon/quickphrase/editor"};
    OptionWithAnnotation<std::vector<std::string>, ToolTipAnnotation>
        quickphraseTriggerRegex{
            this,
            "QuickPhraseTriggerRegex",
            _("Regular expression to trigger quick phrase"),
            {".(/|@)$", "^(www|bbs|forum|mail|bbs)\\.",
             "^(http|https|ftp|telnet|mailto):"},
            {},
            {},
            {_("Enter quickphrase mode when current input matches any regular "
               "expression from the list.")}};
    Option<FuzzyConfig> fuzzyConfig{this, "Fuzzy", _("Fuzzy Pinyin")};
    HiddenOption<bool> firstRun{this, "FirstRun", "FirstRun", true};)

struct EventSourceTime;
class CandidateList;
class PinyinEngine;

enum class PinyinMode { Normal, StrokeFilter, ForgetCandidate, Punctuation };

class PinyinState : public InputContextProperty {
public:
    PinyinState(PinyinEngine *engine);

    libime::PinyinContext context_;
    bool lastIsPunc_ = false;

    PinyinMode mode_ = PinyinMode::Normal;

    // Stroke filter
    std::shared_ptr<CandidateList> strokeCandidateList_;
    InputBuffer strokeBuffer_;

    // Forget candidate
    std::shared_ptr<CandidateList> forgetCandidateList_;

    std::unique_ptr<EventSourceTime> cancelLastEvent_;

    std::optional<std::vector<std::string>> predictWords_;

    int keyReleased_ = -1;
    int keyReleasedIndex_ = -2;
    uint64_t lastKeyPressedTime_ = 0;
};

class PinyinEngine final : public InputMethodEngineV3,
                           public TrackableObject<PinyinEngine> {
public:
    PinyinEngine(Instance *instance);
    ~PinyinEngine() override;
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void doReset(InputContext *inputContext) const;
    void save() override;
    auto &factory() { return factory_; }
    std::string subMode(const InputMethodEntry &entry,
                        InputContext &inputContext) override;
    void invokeActionImpl(const InputMethodEntry &entry,
                          InvokeActionEvent &event) override;

    const Configuration *getConfig() const override;
    const Configuration *
    getConfigForInputMethod(const InputMethodEntry &entry) const override;

    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, "conf/pinyin.conf");
        populateConfig();
    }

    void setSubConfig(const std::string &path,
                      const fcitx::RawConfig &config) override;

    libime::PinyinIME *ime() { return ime_.get(); }
    const auto &config() const { return config_; }

    void initPredict(InputContext *inputContext);
    void updatePredict(InputContext *inputContext);

    void updateUI(InputContext *inputContext);

    void resetStroke(InputContext *inputContext) const;
    void resetForgetCandidate(InputContext *inputContext) const;
    void forgetCandidate(InputContext *inputContext, size_t index);
    void pinCustomPhrase(InputContext *inputContext,
                         const std::string &customPhrase);
    void deleteCustomPhrase(InputContext *inputContext,
                            const std::string &customPhrase);

    FCITX_ADDON_DEPENDENCY_LOADER(cloudpinyin, instance_->addonManager());

    const auto &selectionKeys() const { return selectionKeys_; }

private:
    void cloudPinyinSelected(InputContext *inputContext,
                             const std::string &selected,
                             const std::string &word);

    bool handleCloudpinyinTrigger(KeyEvent &event);
    bool handle2nd3rdSelection(KeyEvent &event);
    bool handleCandidateList(KeyEvent &event,
                             const std::shared_future<uint32_t> &keyChr);
    bool handleNextPage(KeyEvent &event) const;
    bool handleStrokeFilter(KeyEvent &event,
                            const std::shared_future<uint32_t> &keyChr);
    bool handleForgetCandidate(KeyEvent &event);
    bool handlePunc(KeyEvent &event,
                    const std::shared_future<uint32_t> &keyChr);
    bool handlePuncCandidate(KeyEvent &event);
    bool handleCompose(KeyEvent &event);
    void resetPredict(InputContext *inputContext);

    std::string evaluateCustomPhrase(InputContext *inputContext,
                                     std::string_view key);

    void populateConfig();

    void updateStroke(InputContext *inputContext);
    void updateForgetCandidate(InputContext *inputContext);

    void updatePreedit(InputContext *inputContext) const;
    void updatePuncCandidate(InputContext *inputContext,
                             const std::string &original,
                             const std::vector<std::string> &candidates) const;
    void updatePuncPreedit(InputContext *inputContext) const;

    std::pair<Text, Text> preedit(InputContext *inputContext) const;
    std::string preeditCommitString(InputContext *inputContext) const;

#ifdef FCITX_HAS_LUA
    std::vector<std::string>
    luaCandidateTrigger(InputContext *ic, const std::string &candidateString);
#endif
    void loadBuiltInDict();
    void loadExtraDict();
    void loadCustomPhrase();
    void loadSymbols(const UnixFD &file);
    void loadDict(const std::string &fullPath,
                  std::list<std::unique_ptr<TaskToken>> &taskTokens);
    void saveCustomPhrase();

    Instance *instance_;
    PinyinEngineConfig config_;
    PinyinEngineConfig pyConfig_;
    std::unique_ptr<libime::PinyinIME> ime_;
    std::vector<std::regex> quickphraseTriggerRegex_;
    KeyList selectionKeys_;
    KeyList numpadSelectionKeys_;
    FactoryFor<PinyinState> factory_;
    SimpleAction predictionAction_;
    libime::PinyinPrediction prediction_;
    std::unique_ptr<EventSource> deferEvent_;
    std::unique_ptr<EventSource> deferredPreload_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> event_;
    CustomPhraseDict customPhrase_;
    SymbolDict symbols_;
    WorkerThread worker_;
    std::list<std::unique_ptr<TaskToken>> persistentTask_;
    std::list<std::unique_ptr<TaskToken>> tasks_;

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(pinyinhelper, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(spell, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(imeapi, instance_->addonManager());

    static constexpr size_t NumBuiltInDict = 2;
};

} // namespace fcitx

#endif // _PINYIN_PINYIN_H_
