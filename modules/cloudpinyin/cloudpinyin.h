/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CLOUDPINYIN_CLOUDPINYIN_H_
#define _CLOUDPINYIN_CLOUDPINYIN_H_

#include "cloudpinyin_public.h"
#include "fetch.h"
#include "lrucache.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/misc.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>

FCITX_CONFIG_ENUM(CloudPinyinBackend, Google, GoogleCN, Baidu);
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
                                              CloudPinyinBackend::GoogleCN};
    fcitx::OptionWithAnnotation<std::string, fcitx::ToolTipAnnotation> proxy{
        this,
        "Proxy",
        _("Proxy"),
        "",
        {},
        {},
        {_("The proxy format must be the one that is supported by cURL. "
           "Usually it is in the format of [scheme]://[host]:[port], e.g. "
           "http://localhost:1080.")}};);

class Backend {
public:
    virtual void prepareRequest(CurlQueue *queue,
                                const std::string &pinyin) = 0;
    virtual std::string parseResult(CurlQueue *queue) = 0;
    virtual ~Backend() = default;
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
    }

    void request(const std::string &pinyin, CloudPinyinCallback callback);
    const fcitx::KeyList &toggleKey() const {
        return config_.toggleKey.value();
    }
    void resetError() {
        errorCount_ = 0;
        resetError_->setEnabled(false);
    }

    void notifyFinished();

private:
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, request);
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, toggleKey);
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, resetError);
    std::unique_ptr<FetchThread> thread_;
    fcitx::EventLoop *eventLoop_;
    fcitx::EventDispatcher dispatcher_;
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
