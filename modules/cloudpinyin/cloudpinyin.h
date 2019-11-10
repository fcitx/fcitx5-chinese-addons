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
#ifndef _CLOUDPINYIN_CLOUDPINYIN_H_
#define _CLOUDPINYIN_CLOUDPINYIN_H_

#include "cloudpinyin_public.h"
#include "fetch.h"
#include "lrucache.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/unixfd.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>

FCITX_CONFIG_ENUM(CloudPinyinBackend, Google, Baidu);
FCITX_CONFIGURATION(
    CloudPinyinConfig,
    fcitx::Option<fcitx::KeyList> toggleKey{
        this,
        "Toggle Key",
        _("Toggle Key"),
        {fcitx::Key("Control+Alt+Shift+C")}};
    fcitx::Option<int> minimumLength{this, "MinimumPinyinLength",
                                     _("Minimum Pinyin Length"), 4};
    fcitx::Option<CloudPinyinBackend> backend{this, "Backend", _("Backend"),
                                              CloudPinyinBackend::Google};);

class Backend {
public:
    virtual void prepareRequest(CurlQueue *queue,
                                const std::string &pinyin) = 0;
    virtual std::string parseResult(CurlQueue *queue) = 0;
};

class CloudPinyin : public fcitx::AddonInstance {
public:
    CloudPinyin(fcitx::AddonManager *manager);
    ~CloudPinyin();

    void reloadConfig() override;
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override {
        config_.load(config, true);
        fcitx::safeSaveAsIni(config_, "conf/cloudpinyin.conf");
        reloadConfig();
    }

    void request(const std::string &pinyin, CloudPinyinCallback callback);
    const fcitx::KeyList &toggleKey() { return config_.toggleKey.value(); }
    void resetError() {
        errorCount_ = 0;
        resetError_->setEnabled(false);
    }

private:
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, request);
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, toggleKey);
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, resetError);
    fcitx::UnixFD recvFd_, notifyFd_;
    std::unique_ptr<FetchThread> thread_;
    fcitx::EventLoop *eventLoop_;
    std::unique_ptr<fcitx::EventSourceIO> event_;
    std::unique_ptr<fcitx::EventSourceTime> resetError_;
    LRUCache<std::string, std::string> cache_{2048};
    std::unordered_map<CloudPinyinBackend, std::unique_ptr<Backend>,
                       fcitx::EnumHash>
        backends_;
    CloudPinyinConfig config_;
    int errorCount_ = 0;
};

class CloudPinyinFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new CloudPinyin(manager);
    }
};

#endif // _CLOUDPINYIN_CLOUDPINYIN_H_
