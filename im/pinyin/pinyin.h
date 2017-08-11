/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
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
#ifndef _PINYIN_PINYIN_H_
#define _PINYIN_PINYIN_H_

#include <fcitx-config/configuration.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <libime/pinyinime.h>
#include <memory>

namespace fcitx {

FCITX_CONFIG_ENUM(ShuangpinProfileEnum, Ziranma, MS, Ziguang, ABC,
                  Zhongwenzhixing, PinyinJiajia, Xiaohe, Custom)

FCITX_CONFIGURATION(FuzzyConfig,
                    Option<bool> ue{this, "VE_UE", "ue -> ve", true};
                    Option<bool> ng{this, "NG_GN", "gn -> ng", true};
                    Option<bool> inner{this, "Inner", "Inner Segment (xian -> xi'an)", true};
                    Option<bool> v{this, "V_U", "u <-> v", false};
                    Option<bool> an{this, "AN_ANG", "an <-> ang", false};
                    Option<bool> en{this, "EN_ENG", "en <-> eng", false};
                    Option<bool> ian{this, "IAN_IANG", "ian <-> iang", false};
                    Option<bool> in{this, "IN_ING", "in <-> ing", false};
                    Option<bool> ou{this, "U_OU", "ue -> ve", false};
                    Option<bool> uan{this, "UAN_UANG", "ue -> ve", false};
                    Option<bool> c{this, "C_CH", "ue -> ve", false};
                    Option<bool> f{this, "F_H", "ue -> ve", false};
                    Option<bool> l{this, "L_N", "ue -> ve", false};
                    Option<bool> s{this, "S_SH", "ue -> ve", false};
                    Option<bool> z{this, "Z_ZH", "ue -> ve", false};)

FCITX_CONFIGURATION(
    PinyinEngineConfig,
    Option<int, IntConstrain> pageSize{this, "PageSize", "Page size", 5,
                                       IntConstrain(3, 10)};
    Option<bool> cloudPinyinEnabled{this, "CloudPinyin/Enabled",
                                    "Cloud Pinyin Enabled", true};
    Option<int, IntConstrain> cloudPinyinIndex{this, "CloudPinyin/Index",
                                               "Cloud Pinyin Index", 2,
                                               IntConstrain(1, 10)};
    Option<KeyList> prevPage{this,
                             "Prev Page",
                             "Prev Page",
                             {Key(FcitxKey_minus), Key(FcitxKey_Up)}};
    Option<KeyList> nextPage{this,
                             "Next Page",
                             "Next Page",
                             {Key(FcitxKey_equal), Key(FcitxKey_Down)}};
    Option<int, IntConstrain> nbest{this, "Number of sentence",
                                    "Number of Sentence", 2,
                                    IntConstrain(1, 3)};
    Option<ShuangpinProfileEnum> shuangpinProfile{
        this, "Shuangpin Profile", "Shuangpin Profile",
        ShuangpinProfileEnum::Ziranma};
    Option<FuzzyConfig> fuzzyConfig{this, "Fuzzy", "Fuzzy Pinyin Settings"};);

class PinyinState;

class PinyinEngine : public InputMethodEngine {
public:
    PinyinEngine(Instance *instance);
    ~PinyinEngine();
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    std::vector<InputMethodEntry> listInputMethods() override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void save() override;
    auto &factory() { return factory_; }

    libime::PinyinIME *ime() { return ime_.get(); }

    void updateUI(InputContext *inputContext);

private:
    Instance *instance_;
    PinyinEngineConfig config_;
    std::unique_ptr<libime::PinyinIME> ime_;
    KeyList selectionKeys_;
    FactoryFor<PinyinState> factory_;
    bool firstActivate_ = false;

    FCITX_ADDON_DEPENDENCY_LOADER(quickphrase, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(cloudpinyin, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager());
    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());
};

class PinyinEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new PinyinEngine(manager->instance());
    }
};
}

#endif // _PINYIN_PINYIN_H_
