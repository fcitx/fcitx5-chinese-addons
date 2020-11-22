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
#include <fcitx-config/iniparser.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <libime/pinyin/pinyindictionary.h>
#include <memory>

namespace fcitx {

class TableState;

FCITX_CONFIGURATION(TableGlobalConfig,
                    KeyListOption modifyDictionary{this,
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
                                               _("Lookup pinyin"),
                                               {Key("Control+Alt+E")},
                                               KeyListConstrain()};);

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
        safeSaveAsIni(config_, "conf/table.conf");
    }

    const Configuration *
    getConfigForInputMethod(const InputMethodEntry &) const override;
    void setConfigForInputMethod(const InputMethodEntry &,
                                 const RawConfig &) override;

    const libime::PinyinDictionary &pinyinDict();
    const libime::LanguageModel &pinyinModel();

    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(pinyinhelper, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());

private:
    void cloudTableSelected(InputContext *inputContext,
                            const std::string &selected,
                            const std::string &word);

    Instance *instance_;
    std::unique_ptr<TableIME> ime_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> events_;
    FactoryFor<TableState> factory_;

    TableGlobalConfig config_;
    libime::PinyinDictionary pinyinDict_;
    bool pinyinLoaded_ = false;
    std::unique_ptr<libime::LanguageModel> pinyinLM_;
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
