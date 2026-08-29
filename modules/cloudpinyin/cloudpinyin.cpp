/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "cloudpinyin.h"
#include "backend.h"
#include "cloudpinyin_public.h"
#include "fetch.h"
#include <cstdint>
#include <curl/curl.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/unixfd.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>

namespace fcitx::cloudpinyin {

namespace {

#define CLOUDPINYIN_DEBUG() FCITX_LOGC(cloudpinyin, Debug)
FCITX_DEFINE_LOG_CATEGORY(cloudpinyin, "cloudpinyin");

constexpr int MAX_ERROR = 10;
constexpr uint64_t minInUs = 60000000;

} // namespace

CloudPinyin::CloudPinyin(fcitx::AddonManager *manager)
    : eventLoop_(manager->eventLoop()),
      dispatcher_(manager->instance()->eventDispatcher()) {
    curl_global_init(CURL_GLOBAL_ALL);

    backends_.emplace(CloudPinyinBackend::Google,
                      createBackend(CloudPinyinBackend::Google));
    backends_.emplace(CloudPinyinBackend::GoogleCN,
                      createBackend(CloudPinyinBackend::GoogleCN));
    backends_.emplace(CloudPinyinBackend::Baidu,
                      createBackend(CloudPinyinBackend::Baidu));

    resetError_ =
        eventLoop_->addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), minInUs,
                                 [this](EventSourceTime *, uint64_t) {
                                     resetError();
                                     return true;
                                 });
    if (resetError_) {
        resetError_->setEnabled(false);
    }
    thread_ = std::make_unique<FetchThread>(this);

    reloadConfig();
}

CloudPinyin::~CloudPinyin() {}

void CloudPinyin::reloadConfig() {
    readAsIni(config_, "conf/cloudpinyin.conf");
}

void CloudPinyin::request(const std::string &pinyin,
                          CloudPinyinCallback callback) {
    if (static_cast<int>(pinyin.size()) < config_.minimumLength.value()) {
        callback(pinyin, "");
        return;
    }
    if (auto *value = cache_.find(pinyin)) {
        callback(pinyin, *value);
    } else {
        auto backend = config_.backend.value();
        auto iter = backends_.find(backend);
        if (iter == backends_.end() || errorCount_ >= MAX_ERROR) {
            callback(pinyin, "");
            return;
        }
        auto *b = iter->second.get();
        if (!thread_->addRequest([proxy = *config_.proxy, b, &pinyin,
                                  &callback](CurlQueue *queue) {
                const auto url = b->prepareRequest(pinyin);
                if (url.empty()) {
                    return false;
                }
                CLOUDPINYIN_DEBUG() << "Request URL: " << url;
                if (curl_easy_setopt(queue->curl(), CURLOPT_URL, url.c_str()) !=
                    CURLE_OK) {
                    return false;
                }
                if (curl_easy_setopt(
                        queue->curl(), CURLOPT_PROXY,
                        (proxy.empty() ? nullptr : proxy.data())) != CURLE_OK) {
                    return false;
                }
                queue->setPinyin(pinyin);
                queue->setBusy();
                queue->setCallback(callback);
                return true;
            })) {
            callback(pinyin, "");
        }
    }
}

void CloudPinyin::notifyFinished() {
    dispatcher_.scheduleWithContext(this->watch(), [this]() {
        CurlQueue *item;
        auto backend = config_.backend.value();
        auto iter = backends_.find(backend);
        Backend *b = nullptr;
        if (iter != backends_.end()) {
            b = iter->second.get();
        }

        while ((item = thread_->popFinished())) {
            if (item->httpCode() != 200) {
                errorCount_ += 1;

                if (errorCount_ == MAX_ERROR && resetError_) {
                    FCITX_ERROR() << "Cloud pinyin reaches max error. "
                                     "Retry in 5 minutes.";
                    resetError_->setNextInterval(minInUs * 5);
                    resetError_->setOneShot();
                }
            }

            std::string hanzi;
            if (b) {
                const std::string_view result(item->result().data(),
                                              item->result().size());
                CLOUDPINYIN_DEBUG() << "Request result: " << result;
                hanzi = b->parseResult(result);
            } else {
                hanzi = "";
            }
            item->callback()(item->pinyin(), hanzi);
            if (!hanzi.empty()) {
                cache_.insert(item->pinyin(), hanzi);
            }
            item->release();
        }
        return true;
    });
}

FCITX_ADDON_FACTORY_V2(cloudpinyin, CloudPinyinFactory);

} // namespace fcitx::cloudpinyin
