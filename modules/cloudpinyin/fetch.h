/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CLOUDPINYIN_FETCH_H_
#define _CLOUDPINYIN_FETCH_H_

#include "backend.h"
#include "cloudpinyin_public.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/intrusivelist.h>
#include <fcitx-utils/misc.h>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#define MAX_HANDLE 100l
#define MAX_BUFFER_SIZE 2048

namespace fcitx::cloudpinyin {

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
            (curl_easy_setopt(curl_, CURLOPT_HEADERDATA, this) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION,
                              &CurlQueue::curlHeaderFunction) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L) == CURLE_OK) &&
            (curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L) == CURLE_OK);
        if (!result) {
            throw std::runtime_error("Failed setup CURL handle options.");
        }
    }

    ~CurlQueue() override {
        release();
        curl_easy_cleanup(curl_);
    }

    void release() {
        busy_ = false;
        data_.clear();
        headers_.clear();
        headerSize_ = 0;
        context_ = {};
        requestBody_.clear();
        // make sure lambda is free'd
        callback_ = CloudPinyinResultCallback();
        backend_.reset();
        httpCode_ = 0;
        curlResult_ = CURLE_OK;
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, nullptr);
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, nullptr);
        curl_easy_setopt(curl_, CURLOPT_POST, 0L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl_, CURLOPT_PROXY, nullptr);
        requestHeaders_.reset();
    }

    const auto &context() const { return context_; }
    void setContext(CloudPinyinRequestContext context) {
        context_ = std::move(context);
    }

    auto curl() { return curl_; }
    void finish(CURLcode result) {
        curlResult_ = result;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode_);
    }

    bool busy() const { return busy_; }
    void setBusy() { busy_ = true; }

    const std::vector<char> &result() const { return data_; }
    const HTTPHeaders &headers() const { return headers_; }

    CloudPinyinResultCallback callback() { return callback_; }
    void setCallback(CloudPinyinResultCallback callback) {
        callback_ = std::move(callback);
    }

    auto httpCode() const { return httpCode_; }
    bool succeeded() const { return curlResult_ == CURLE_OK; }

    bool setupRequest(const HTTPRequest &request, const std::string &proxy) {
        if (request.method == "GET" && !request.body.empty()) {
            return false;
        }
        UniqueCPtr<curl_slist, curl_slist_free_all> requestHeaders;
        for (const auto &[name, value] : request.headers) {
            auto *oldHeaders = requestHeaders.release();
            auto *headers =
                curl_slist_append(oldHeaders, (name + ": " + value).c_str());
            if (!headers) {
                requestHeaders.reset(oldHeaders);
                return false;
            }
            requestHeaders.reset(headers);
        }
        requestHeaders_ = std::move(requestHeaders);
        requestBody_ = request.body;
        const bool hasBody = !requestBody_.empty();
        const bool isPost = request.method == "POST";
        const bool needsPostFields = hasBody || isPost;
        return curl_easy_setopt(curl_, CURLOPT_URL, request.url.c_str()) ==
                   CURLE_OK &&
               curl_easy_setopt(curl_, CURLOPT_PROXY,
                                proxy.empty() ? nullptr : proxy.c_str()) ==
                   CURLE_OK &&
               curl_easy_setopt(curl_, CURLOPT_TIMEOUT, request.timeout) ==
                   CURLE_OK &&
               curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,
                                requestHeaders_.get()) == CURLE_OK &&
               curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST,
                                request.method == "GET"
                                    ? nullptr
                                    : request.method.c_str()) == CURLE_OK &&
               curl_easy_setopt(curl_, CURLOPT_HTTPGET,
                                !isPost && !hasBody ? 1L : 0L) == CURLE_OK &&
               (!isPost ||
                curl_easy_setopt(curl_, CURLOPT_POST, 1L) == CURLE_OK) &&
               (!needsPostFields ||
                (curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,
                                  requestBody_.c_str()) == CURLE_OK &&
                 curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,
                                  static_cast<long>(requestBody_.size())) ==
                     CURLE_OK));
    }

    void setBackend(std::shared_ptr<Backend> backend) {
        backend_ = std::move(backend);
    }
    const std::shared_ptr<Backend> &backend() const { return backend_; }

private:
    static size_t curlWriteFunction(char *ptr, size_t size, size_t nmemb,
                                    void *userdata) {
        auto *self = static_cast<CurlQueue *>(userdata);
        return self->curlWrite(ptr, size, nmemb);
    }

    static size_t curlHeaderFunction(char *ptr, size_t size, size_t nmemb,
                                     void *userdata) {
        auto *self = static_cast<CurlQueue *>(userdata);
        return self->curlHeader(ptr, size, nmemb);
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

        realsize = std::min(SIZE_MAX - data_.size(), realsize);

        // make sure we won't be hacked
        if (data_.size() + realsize > MAX_BUFFER_SIZE) {
            return 0;
        }

        data_.reserve(data_.size() + realsize);
        std::copy(ptr, ptr + realsize, std::back_inserter(data_));
        return realsize;
    }

    size_t curlHeader(char *ptr, size_t size, size_t nmemb) {
        const size_t realSize = size * nmemb;
        if (realSize > MAX_BUFFER_SIZE ||
            headerSize_ > MAX_BUFFER_SIZE - realSize) {
            return 0;
        }
        headerSize_ += realSize;
        std::string_view header(ptr, realSize);
        const auto colon = header.find(':');
        if (colon != std::string_view::npos) {
            const auto valueStart = header.find_first_not_of(" \t", colon + 1);
            const auto valueEnd = header.find_last_not_of("\r\n");
            if (valueStart != std::string_view::npos &&
                valueEnd != std::string_view::npos && valueStart <= valueEnd) {
                headers_.emplace_back(
                    header.substr(0, colon),
                    header.substr(valueStart, valueEnd - valueStart + 1));
            }
        }
        return realSize;
    }

    bool busy_ = false;
    CURL *curl_ = nullptr;
    CURLcode curlResult_ = CURLE_OK;
    long httpCode_ = 0;
    std::vector<char> data_;
    HTTPHeaders headers_;
    size_t headerSize_ = 0;
    CloudPinyinRequestContext context_;
    std::string requestBody_;
    UniqueCPtr<curl_slist, curl_slist_free_all> requestHeaders_;
    CloudPinyinResultCallback callback_;
    std::shared_ptr<Backend> backend_;
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
    int curl(curl_socket_t s,                     /* socket */
             int action);
    void curlTimer(long timeout_ms);

    void handleIO(int fd, fcitx::IOEventFlags flags);
    void processMessages();

    void handlePendingRequests();

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

} // namespace fcitx::cloudpinyin

#endif // _CLOUDPINYIN_FETCH_H_
