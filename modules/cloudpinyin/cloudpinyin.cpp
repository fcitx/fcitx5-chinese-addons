/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "cloudpinyin.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/unixfd.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

using namespace fcitx;

FCITX_DEFINE_LOG_CATEGORY(cloudpinyin, "cloudpinyin");

#define CLOUDPINYIN_DEBUG() FCITX_LOGC(cloudpinyin, Debug)

class GoogleBackend : public Backend {
public:
    GoogleBackend(const std::string &url) : url_(url) {}

    void prepareRequest(CurlQueue *queue, const std::string &pinyin) override {
        UniqueCPtr<char, curl_free> escaped(
            curl_escape(pinyin.c_str(), pinyin.size()));
        std::string url = url_;
        url += escaped.get();
        CLOUDPINYIN_DEBUG() << "Request URL: " << url;
        curl_easy_setopt(queue->curl(), CURLOPT_URL, url.c_str());
    }
    std::string parseResult(CurlQueue *queue) override {
        std::string result(queue->result().begin(), queue->result().end());
        CLOUDPINYIN_DEBUG() << "Request result: " << result;
        auto start = result.find("\",[\"");
        std::string hanzi;
        if (start != std::string::npos) {
            start += strlen("\",[\"");
            auto end = result.find('\"', start);
            if (end != std::string::npos && end > start) {
                hanzi = result.substr(start, end - start);
            }
        }
        return hanzi;
    }

private:
    const std::string url_;
};

class BaiduBackend : public Backend {
public:
    void prepareRequest(CurlQueue *queue, const std::string &pinyin) override {
        std::string url = "https://olime.baidu.com/py?rn=0&pn=1&ol=1&py=";
        std::unique_ptr<char, decltype(&curl_free)> escaped(
            curl_escape(pinyin.c_str(), pinyin.size()), &curl_free);
        url += escaped.get();
        CLOUDPINYIN_DEBUG() << "Request URL: " << url;
        curl_easy_setopt(queue->curl(), CURLOPT_URL, url.c_str());
    }

    std::string parseResult(CurlQueue *queue) override {
        std::string result(queue->result().begin(), queue->result().end());
        CLOUDPINYIN_DEBUG() << "Request result: " << result;
        auto start = result.find("[[\"");
        std::string hanzi;
        if (start != std::string::npos) {
            start += strlen("[[\"");
            auto end = result.find("\",", start);
            if (end != std::string::npos && end > start) {
                hanzi = result.substr(start, end - start);
            }
        }
        return hanzi;
    }
};

constexpr int MAX_ERROR = 10;
constexpr int minInUs = 60000000;

CloudPinyin::CloudPinyin(fcitx::AddonManager *manager)
    : eventLoop_(manager->eventLoop()) {
    curl_global_init(CURL_GLOBAL_ALL);
    dispatcher_.attach(eventLoop_);

    backends_.emplace(
        CloudPinyinBackend::Google,
        std::make_unique<GoogleBackend>(
            "https://www.google.com/inputtools/request?ime=pinyin&text="));
    backends_.emplace(
        CloudPinyinBackend::GoogleCN,
        std::make_unique<GoogleBackend>(
            "https://www.google.cn/inputtools/request?ime=pinyin&text="));
    backends_.emplace(CloudPinyinBackend::Baidu,
                      std::make_unique<BaiduBackend>());

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

CloudPinyin::~CloudPinyin() { dispatcher_.detach(); }

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
                b->prepareRequest(queue, pinyin);
                if (proxy.empty()) {
                    curl_easy_setopt(queue->curl(), CURLOPT_PROXY, nullptr);
                } else {
                    curl_easy_setopt(queue->curl(), CURLOPT_PROXY,
                                     proxy.data());
                }
                queue->setPinyin(pinyin);
                queue->setBusy();
                queue->setCallback(callback);
            })) {
            callback(pinyin, "");
        };
    }
}

void CloudPinyin::notifyFinished() {
    dispatcher_.schedule([this]() {
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
                hanzi = b->parseResult(item);
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

FCITX_ADDON_FACTORY(CloudPinyinFactory);
