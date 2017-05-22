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
#ifndef _CLOUDPINYIN_CLOUDPINYIN_H_
#define _CLOUDPINYIN_CLOUDPINYIN_H_

#include "cloudpinyin_public.h"
#include "fetch.h"
#include "lrucache.h"
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/unixfd.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>

FCITX_CONFIG_ENUM(CloudPinyinBackend, Google, Baidu);
FCITX_CONFIGURATION(
    CloudPinyinConfig,
    fcitx::Option<fcitx::KeyList> toggleKey{
        this, "Toggle Key", "Toggle Key", {fcitx::Key("Control+Alt+Shift+c")}};
    fcitx::Option<int> minimumLength{this, "MinimumPinyinLength",
                                     "MinimumPinyinLength", 4};
    fcitx::Option<CloudPinyinBackend> backend{this, "Backend", "Backend",
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

    void request(const std::string &pinyin, CloudPinyinCallback callback);

private:
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, request);
    fcitx::UnixFD recvFd_, notifyFd_;
    std::unique_ptr<FetchThread> thread_;
    fcitx::EventLoop *eventLoop_;
    std::unique_ptr<fcitx::EventSourceIO> event_;
    LRUCache<std::string, std::string> cache_{2048};
    std::unordered_map<CloudPinyinBackend, std::unique_ptr<Backend>,
                       fcitx::EnumHash>
        backends_;
    CloudPinyinConfig config_;
};

class CloudPinyinFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new CloudPinyin(manager);
    }
};

#endif // _CLOUDPINYIN_CLOUDPINYIN_H_
