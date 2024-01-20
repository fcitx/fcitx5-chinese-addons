/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CLOUDPINYIN_FETCH_H_
#define _CLOUDPINYIN_FETCH_H_

#include "cloudpinyin_public.h"
#include <cstdint>
#include <curl/curl.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/intrusivelist.h>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#define MAX_HANDLE 100l
#define MAX_BUFFER_SIZE 2048

class CloudPinyin;

class CurlQueue : public fcitx::IntrusiveListNode {
public:
    CurlQueue() : curl_(curl_easy_init()) {
        if (!curl_) {
            throw std::runtime_error("Failed to init CURL handle.");
        }

        // These options should be pretty safe to set, throw exception
        // to de-init cloudpinyin.
        const bool result =
            (curl_easy_setopt(curl_, CURLOPT_PRIVATE, this) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,
                              &CurlQueue::curlWriteFunction) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L) == CURLE_OK);
        if (!result) {
            throw std::runtime_error("Failed setup CURL handle options.");
        }
    }

    ~CurlQueue() override { curl_easy_cleanup(curl_); }

    void release() {
        busy_ = false;
        data_.clear();
        pinyin_.clear();
        // make sure lambda is free'd
        callback_ = CloudPinyinCallback();
        httpCode_ = 0;
    }

    const auto &pinyin() const { return pinyin_; }
    void setPinyin(std::string pinyin) { pinyin_ = std::move(pinyin); }

    auto curl() { return curl_; }
    void finish(CURLcode result) {
        curlResult_ = result;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode_);
    }

    bool busy() const { return busy_; }
    void setBusy() { busy_ = true; }

    const std::vector<char> &result() { return data_; }

    CloudPinyinCallback callback() { return callback_; }
    void setCallback(CloudPinyinCallback callback) {
        callback_ = std::move(callback);
    }

    auto httpCode() const { return httpCode_; }

private:
    static size_t curlWriteFunction(char *ptr, size_t size, size_t nmemb,
                                    void *userdata) {
        auto *self = static_cast<CurlQueue *>(userdata);
        return self->curlWrite(ptr, size, nmemb);
    }

    size_t curlWrite(char *ptr, size_t size, size_t nmemb) {
        size_t realsize = size * nmemb;
        /*
         * We know that it isn't possible to overflow during multiplication if
         * neither operand uses any of the most significant half of the bits in
         * a size_t.
         */

        if ((unsigned long long)((nmemb | size) & ((unsigned long long)SIZE_MAX
                                                   << (sizeof(size_t) << 2))) &&
            (realsize / size != nmemb)) {
            return 0;
        }

        if (SIZE_MAX - data_.size() < realsize) {
            realsize = SIZE_MAX - data_.size();
        }

        // make sure we won't be hacked
        if (data_.size() + realsize > MAX_BUFFER_SIZE) {
            return 0;
        }

        data_.reserve(data_.size() + realsize);
        std::copy(ptr, ptr + realsize, std::back_inserter(data_));
        return realsize;
    }

    bool busy_ = false;
    CURL *curl_ = nullptr;
    CURLcode curlResult_ = CURLE_OK;
    long httpCode_ = 0;
    std::vector<char> data_;
    std::string pinyin_;
    CloudPinyinCallback callback_;
};

using SetupRequestCallback = std::function<bool(CurlQueue *)>;

class FetchThread {
public:
    FetchThread(CloudPinyin *cloudPinyin);
    ~FetchThread();

    // Call from main thread.
    bool addRequest(const SetupRequestCallback &callback);
    CurlQueue *popFinished();

private:
    static void runThread(FetchThread *self);
    static int curlCallback(CURL *easy,      /* easy handle */
                            curl_socket_t s, /* socket */
                            int action,      /* see values below */
                            void *userp,     /* private callback pointer */
                            void *socketp);
    static int curlTimerCallback(CURLM *multi,    /* multi handle */
                                 long timeout_ms, /* see above */
                                 void *userp);    /* private callback
                                                     pointer */
    void curl(curl_socket_t s,                    /* socket */
              int action);
    void curlTimer(long timeout_ms);

    void handleIO(int fd, fcitx::IOEventFlags flags);
    void processMessages();

    void run();
    void finished(CurlQueue *queue);

    // Call from main thread.
    void exit();

    CloudPinyin *cloudPinyin_;
    std::unique_ptr<std::thread> thread_;
    std::unique_ptr<fcitx::EventLoop> loop_;
    fcitx::EventDispatcher dispatcher_;
    std::unordered_map<int, std::unique_ptr<fcitx::EventSourceIO>> events_;
    std::unique_ptr<fcitx::EventSourceTime> timer_;

    CURLM *curlm_;

    CurlQueue handles_[MAX_HANDLE];
    fcitx::IntrusiveList<CurlQueue> pendingQueue;
    fcitx::IntrusiveList<CurlQueue> workingQueue;
    fcitx::IntrusiveList<CurlQueue> finishingQueue;

    std::mutex pendingQueueLock;
    std::mutex finishQueueLock;
};

#endif // _CLOUDPINYIN_FETCH_H_
