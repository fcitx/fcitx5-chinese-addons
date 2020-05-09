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
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <libime/core/prediction.h>
#include <libime/pinyin/pinyinime.h>
#include <memory>

namespace fcitx {

FCITX_CONFIG_ENUM(ShuangpinProfileEnum, Ziranma, MS, Ziguang, ABC,
                  Zhongwenzhixing, PinyinJiajia, Xiaohe, Custom)

FCITX_CONFIG_ENUM_I18N_ANNOTATION(ShuangpinProfileEnum, N_("Ziranma"), N_("MS"),
                                  N_("Ziguang"), N_("ABC"),
                                  N_("Zhongwenzhixing"), N_("PinyinJiajia"),
                                  N_("Xiaohe"), N_("Custom"))

FCITX_CONFIGURATION(
    FuzzyConfig, Option<bool> ue{this, "VE_UE", _("ue -> ve"), true};
    Option<bool> ng{this, "NG_GN", _("gn -> ng"), true};
    Option<bool> inner{this, "Inner", _("Inner Segment (xian -> xi'an)"), true};
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
    Option<int, IntConstrain> pageSize{this, "PageSize", _("Page size"), 5,
                                       IntConstrain(3, 10)};
    Option<int, IntConstrain> predictionSize{
        this, "PredictionSize", _("Prediction Size"), 10, IntConstrain(3, 20)};
    Option<bool> predictionEnabled{this, "Prediction", _("Enable Prediction"),
                                   false};
    Option<bool> emojiEnabled{this, "EmojiEnabled", _("Enable Emoji"), true};
    Option<bool> cloudPinyinEnabled{this, "CloudPinyinEnabled",
                                    _("Enable Cloud Pinyin"), true};
    Option<int, IntConstrain> cloudPinyinIndex{this, "CloudPinyinIndex",
                                               _("Cloud Pinyin Index"), 2,
                                               IntConstrain(1, 10)};
    Option<bool> showPreeditInApplication{this, "PreeditInApplication",
                                          _("Show preedit within application"),
                                          false};
    KeyListOption prevPage{
        this,
        "PrevPage",
        _("Prev Page"),
        {Key(FcitxKey_minus), Key(FcitxKey_Up)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextPage{
        this,
        "NextPage",
        _("Next Page"),
        {Key(FcitxKey_equal), Key(FcitxKey_Down)},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption prevCandidate{
        this,
        "PrevCandidate",
        _("Prev Candidate"),
        {Key("Shift+Tab")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    KeyListOption nextCandidate{
        this,
        "NextCandidate",
        _("Next Candidate"),
        {Key("Tab")},
        KeyListConstrain({KeyConstrainFlag::AllowModifierLess})};
    Option<int, IntConstrain> nbest{this, "Number of sentence",
                                    _("Number of Sentence"), 2,
                                    IntConstrain(1, 3)};
    OptionWithAnnotation<ShuangpinProfileEnum,
                         ShuangpinProfileEnumI18NAnnotation>
        shuangpinProfile{this, "ShuangpinProfile", _("Shuangpin Profile"),
                         ShuangpinProfileEnum::Ziranma};
    ExternalOption dictmanager{this, "DictManager", _("Dictionaries"),
                               "fcitx://config/addon/pinyin/dictmanager"};
    Option<FuzzyConfig> fuzzyConfig{this, "Fuzzy",
                                    _("Fuzzy Pinyin Settings")};);

class PinyinState;
class EventSourceTime;
class CandidateList;

class PinyinEngine final : public InputMethodEngine {
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
    void setSubConfig(const std::string &path,
                      const fcitx::RawConfig &) override;
    void doReset(InputContext *ic);
    void save() override;
    auto &factory() { return factory_; }

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, "conf/pinyin.conf");
        reloadConfig();
    }

    libime::PinyinIME *ime() { return ime_.get(); }

    void initPredict(InputContext *ic);
    void updatePredict(InputContext *ic);
    std::unique_ptr<CandidateList>
    predictCandidateList(const std::vector<std::string> &words);
    void updateUI(InputContext *inputContext);

private:
    void cloudPinyinSelected(InputContext *inputContext,
                             const std::string &selected,
                             const std::string &word);
#ifdef FCITX_HAS_LUA
    std::vector<std::string>
    luaCandidateTrigger(InputContext *ic, const std::string &candidateString);
#endif
    void loadExtraDict();
    void loadDict(const StandardPathFile &file);

    Instance *instance_;
    PinyinEngineConfig config_;
    std::unique_ptr<libime::PinyinIME> ime_;
    KeyList selectionKeys_;
    FactoryFor<PinyinState> factory_;
    SimpleAction predictionAction_;
    libime::Prediction prediction_;

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(cloudpinyin, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(pinyinhelper, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(spell, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(imeapi, instance_->addonManager());
};

class PinyinEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new PinyinEngine(manager->instance());
    }
};
} // namespace fcitx

#endif // _PINYIN_PINYIN_H_
