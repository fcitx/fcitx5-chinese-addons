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
    void prepareRequest(CurlQueue *queue, const std::string &pinyin) override {
        std::string url =
            "https://www.google.com/inputtools/request?ime=pinyin&text=";
        std::unique_ptr<char, decltype(&curl_free)> escaped(
            curl_escape(pinyin.c_str(), pinyin.size()), &curl_free);
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
            auto end = result.find("\"", start);
            if (end != std::string::npos && end > start) {
                hanzi = result.substr(start, end - start);
            }
        }
        return hanzi;
    }
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
    UnixFD pipe1Fd[2];

    int pipe1[2];
    if (pipe2(pipe1, O_NONBLOCK) < 0) {
        throw std::runtime_error("Failed to create pipe");
    }
    pipe1Fd[0].give(pipe1[0]);
    pipe1Fd[1].give(pipe1[1]);

    recvFd_.give(pipe1Fd[0].release());

    backends_.emplace(CloudPinyinBackend::Google,
                      std::make_unique<GoogleBackend>());
    backends_.emplace(CloudPinyinBackend::Baidu,
                      std::make_unique<BaiduBackend>());

    event_ = eventLoop_->addIOEvent(
        recvFd_.fd(), IOEventFlag::In,
        [this](EventSourceIO *, int, IOEventFlags) {
            char c;
            while (fs::safeRead(recvFd_.fd(), &c, sizeof(char)) > 0)
                ;
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
                if (hanzi.size()) {
                    cache_.insert(item->pinyin(), hanzi);
                }
                item->release();
            }
            return true;
        });

    resetError_ =
        eventLoop_->addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), minInUs,
                                 [this](EventSourceTime *, uint64_t) {
                                     resetError();
                                     return true;
                                 });
    if (resetError_) {
        resetError_->setEnabled(false);
    }
    thread_ = std::make_unique<FetchThread>(std::move(pipe1Fd[1]));

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
    if (auto value = cache_.find(pinyin)) {
        callback(pinyin, *value);
    } else {
        auto backend = config_.backend.value();
        auto iter = backends_.find(backend);
        if (iter == backends_.end() || errorCount_ >= MAX_ERROR) {
            callback(pinyin, "");
            return;
        }
        auto b = iter->second.get();
        if (!thread_->addRequest([b, &pinyin, &callback](CurlQueue *queue) {
                b->prepareRequest(queue, pinyin);
                queue->setPinyin(pinyin);
                queue->setBusy();
                queue->setCallback(callback);
            })) {
            callback(pinyin, "");
        };
    }
}

FCITX_ADDON_FACTORY(CloudPinyinFactory);
