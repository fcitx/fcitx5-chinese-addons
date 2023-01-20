/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYIN_PINYIN_H_
#define _PINYIN_PINYIN_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <libime/core/prediction.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyinime.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace fcitx {

#ifdef ANDROID
static constexpr bool isAndroid = true;
#else
static constexpr bool isAndroid = false;
#endif

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
    Option<bool> s{this, "S_SH", _("s <-> sh"), false};
    Option<bool> z{this, "Z_ZH", _("z <-> zh"), false};)

FCITX_CONFIGURATION(
    PinyinEngineConfig,
    OptionWithAnnotation<
        ShuangpinProfileEnum,
        OptionalHideInDescriptionBase<ShuangpinProfileEnumI18NAnnotation>>
        shuangpinProfile{this, "ShuangpinProfile", _("Shuangpin Profile"),
                         ShuangpinProfileEnum::Ziranma};
    OptionWithAnnotation<bool, OptionalHideInDescription> showShuangpinMode{
        this, "ShowShuangpinMode", _("Show current shuangpin mode"), true};
    Option<int, IntConstrain> pageSize{this, "PageSize", _("Page size"), 7,
                                       IntConstrain(3, 10)};
    Option<bool> spellEnabled{this, "SpellEnabled", _("Enable Spell"), true};
    Option<bool> emojiEnabled{this, "EmojiEnabled", _("Enable Emoji"), true};
    Option<bool> chaiziEnabled{this, "ChaiziEnabled", _("Enable Chaizi"), true};
    Option<bool> extBEnabled{this, "ExtBEnabled",
                             _("Enable Characters in Unicode CJK Extension B"),
                             !isAndroid};
    OptionWithAnnotation<bool, OptionalHideInDescription> cloudPinyinEnabled{
        this, "CloudPinyinEnabled", _("Enable Cloud Pinyin"), false};
    Option<int, IntConstrain, DefaultMarshaller<int>, OptionalHideInDescription>
        cloudPinyinIndex{this, "CloudPinyinIndex", _("Cloud Pinyin Index"), 2,
                         IntConstrain(1, 10)};
    Option<bool> showPreeditInApplication{this, "PreeditInApplication",
                                          _("Show preedit within application"),
                                          true};
    Option<bool> preeditCursorPositionAtBeginning{
        this, "PreeditCursorPositionAtBeginning",
        _("Fix embedded preedit cursor at the beginning of the preedit"),
        !isAndroid};
    Option<bool> showActualPinyinInPreedit{
        this, "PinyinInPreedit", _("Show complete pinyin in preedit"), false};
    Option<bool> predictionEnabled{this, "Prediction", _("Enable Prediction"),
                                   false};
    Option<int, IntConstrain> predictionSize{
        this, "PredictionSize", _("Prediction Size"), 10, IntConstrain(3, 20)};
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
        {Key(FcitxKey_minus), Key(FcitxKey_Up), Key(FcitxKey_KP_Up)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextPage{
        this,
        "NextPage",
        _("Next Page"),
        {Key(FcitxKey_equal), Key(FcitxKey_Down), Key(FcitxKey_KP_Down)},
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
    KeyListOption secondCandidate{
        this,
        "SecondCandidate",
        _("Select 2nd Candidate"),
        {},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess,
                          KeyConstrainFlag::AllowModifierOnly})};
    KeyListOption thirdCandidate{
        this,
        "ThirdCandidate",
        _("Select 3rd Candidate"),
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
    Option<int, IntConstrain> longWordLimit{
        this, "LongWordLengthLimit",
        _("Prompt long word length when input length over (0 for disable)"), 4,
        IntConstrain(0, 10)};
    ExternalOption dictmanager{this, "DictManager", _("Dictionaries"),
                               "fcitx://config/addon/pinyin/dictmanager"};
    SubConfigOption punctuationMap{
        this, "Punctuation", _("Punctuation"),
        "fcitx://config/addon/punctuation/punctuationmap/zh_CN"};
    SubConfigOption chttrans{
        this, "Chttrans", _("Simplified and Traditional Chinese Translation"),
        "fcitx://config/addon/chttrans"};
    OptionalHiddenSubConfigOption cloudpinyin{
        this, "CloudPinyin", _("Cloud Pinyin"),
        "fcitx://config/addon/cloudpinyin"};
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
        quickphraseTrigger{this,
                           "QuickPhrase trigger",
                           _("Strings to trigger quick phrase"),
                           {"www.", "ftp.", "http:", "mail.", "bbs.", "forum.",
                            "https:", "ftp:", "telnet:", "mailto:"},
                           {},
                           {},
                           {_("Enter a string from the list will make it enter "
                              "quickphrase mode.")}};
    Option<FuzzyConfig> fuzzyConfig{this, "Fuzzy", _("Fuzzy Pinyin Settings")};
    HiddenOption<bool> firstRun{this, "FirstRun", "FirstRun", true};);

class PinyinState;
class EventSourceTime;
class CandidateList;

class PinyinEngine final : public InputMethodEngineV3 {
public:
    PinyinEngine(Instance *instance);
    ~PinyinEngine();
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void doReset(InputContext *inputContext);
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
                      const fcitx::RawConfig &) override;

    libime::PinyinIME *ime() { return ime_.get(); }

    void initPredict(InputContext *inputContext);
    void updatePredict(InputContext *inputContext);
    std::unique_ptr<CandidateList>
    predictCandidateList(const std::vector<std::string> &words);
    void updateUI(InputContext *inputContext);

    void resetStroke(InputContext *inputContext);
    void resetForgetCandidate(InputContext *inputContext);

private:
    void cloudPinyinSelected(InputContext *inputContext,
                             const std::string &selected,
                             const std::string &word);

    bool handleCloudpinyinTrigger(KeyEvent &event);
    bool handle2nd3rdSelection(KeyEvent &event);
    bool handleCandidateList(KeyEvent &event);
    bool handleStrokeFilter(KeyEvent &event);
    bool handleForgetCandidate(KeyEvent &event);
    bool handlePunc(KeyEvent &event);

    void populateConfig();

    void updateStroke(InputContext *inputContext);
    void updateForgetCandidate(InputContext *inputContext);

    void updatePreedit(InputContext *inputContext) const;

    std::pair<Text, Text> preedit(InputContext *inputContext) const;
    std::string preeditCommitString(InputContext *inputContext) const;

#ifdef FCITX_HAS_LUA
    std::vector<std::string>
    luaCandidateTrigger(InputContext *ic, const std::string &candidateString);
#endif
    void loadBuiltInDict();
    void loadExtraDict();
    void loadDict(const StandardPathFile &file);

    Instance *instance_;
    PinyinEngineConfig config_;
    PinyinEngineConfig pyConfig_;
    std::unique_ptr<libime::PinyinIME> ime_;
    std::unordered_map<std::string, std::unordered_set<uint32_t>>
        quickphraseTriggerDict_;
    KeyList selectionKeys_;
    KeyList numpadSelectionKeys_;
    FactoryFor<PinyinState> factory_;
    SimpleAction predictionAction_;
    libime::Prediction prediction_;
    std::unique_ptr<EventSource> deferEvent_;
    std::unique_ptr<EventSource> checkCloudPinyinAvailable_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> event_;

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(cloudpinyin, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(pinyinhelper, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(spell, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(imeapi, instance_->addonManager());

    bool hasCloudPinyin_ = false;

    static constexpr size_t NumBuiltInDict = 3;
};

class PinyinEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new PinyinEngine(manager->instance());
    }
};
} // namespace fcitx

#endif // _PINYIN_PINYIN_H_
