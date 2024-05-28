/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _TABLE_TABLE_H_
#define _TABLE_TABLE_H_

#include "ime.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <libime/core/languagemodel.h>
#include <libime/pinyin/pinyindictionary.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

class TableState;

enum class LookupShuangpinProfileEnum {
    No,
    Ziranma,
    MS,
    Ziguang,
    ABC,
    Zhongwenzhixing,
    PinyinJiajia,
    Xiaohe,
    Custom
};

FCITX_CONFIG_ENUM_NAME_WITH_I18N(LookupShuangpinProfileEnum,
                                 N_("Do not display"), N_("Ziranma"), N_("MS"),
                                 N_("Ziguang"), N_("ABC"),
                                 N_("Zhongwenzhixing"), N_("PinyinJiajia"),
                                 N_("Xiaohe"), N_("Custom"))

FCITX_CONFIGURATION(
    TableGlobalConfig, KeyListOption modifyDictionary{this,
                                                      "ModifyDictionaryKey",
                                                      _("Modify dictionary"),
                                                      {Key("Control+8")},
                                                      KeyListConstrain()};
    KeyListOption forgetWord{this,
                             "ForgetWord",
                             _("Forget word"),
                             {Key("Control+7")},
                             KeyListConstrain()};
    KeyListOption lookupPinyin{this,
                               "LookupPinyinKey",
                               _("Look up pinyin"),
                               {Key("Control+Alt+E")},
                               KeyListConstrain()};
    OptionWithAnnotation<LookupShuangpinProfileEnum,
                         LookupShuangpinProfileEnumI18NAnnotation>
        shuangpinProfile{this, "ShuangpinProfile",
                         _("Display Shuangpin when looking up pinyin"),
                         LookupShuangpinProfileEnum::No};

    Option<bool> predictionEnabled{this, "Prediction", _("Enable Prediction"),
                                   false};
    Option<int, IntConstrain> predictionSize{this, "PredictionSize",
                                             _("Prediction Size"), 10,
                                             IntConstrain(3, 20)};);

class TableEngine final : public InputMethodEngine {
public:
    TableEngine(Instance *instance);
    ~TableEngine();
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    std::string subMode(const InputMethodEntry &entry,
                        InputContext &ic) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void save() override;
    auto &factory() { return factory_; }

    TableIME *ime() { return ime_.get(); }
    auto &config() { return config_; }
    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        populateConfig();
        saveConfig();
    }
    void populateConfig();
    void setSubConfig(const std::string &path,
                      const RawConfig & /*unused*/) override;

    const Configuration *
    getConfigForInputMethod(const InputMethodEntry &entry) const override;
    void setConfigForInputMethod(const InputMethodEntry &entry,
                                 const RawConfig &config) override;

    const libime::PinyinDictionary &pinyinDict();
    const libime::LanguageModel &pinyinModel();
    const auto *reverseShuangPinTable() const {
        return reverseShuangPinTable_.get();
    }

    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(pinyinhelper, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());

private:
    void cloudTableSelected(InputContext *inputContext,
                            const std::string &selected,
                            const std::string &word);
    void saveConfig() { safeSaveAsIni(config_, "conf/table.conf"); }

    void releaseStates();
    void reloadDict();
    void preload();

    Instance *instance_;
    std::unique_ptr<TableIME> ime_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> events_;
    SimpleAction predictionAction_;
    FactoryFor<TableState> factory_;

    TableGlobalConfig config_;
    std::unique_ptr<std::multimap<std::string, std::string>>
        reverseShuangPinTable_;
    libime::PinyinDictionary pinyinDict_;
    bool pinyinLoaded_ = false;
    std::unique_ptr<libime::LanguageModel> pinyinLM_;
    std::unique_ptr<EventSource> preloadEvent_;
};

class TableEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
        return new TableEngine(manager->instance());
    }
};
} // namespace fcitx

#endif // _TABLE_TABLE_H_
