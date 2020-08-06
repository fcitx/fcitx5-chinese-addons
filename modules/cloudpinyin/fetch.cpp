/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "fetch.h"
#include "cloudpinyin.h"
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/fs.h>
#include <fcntl.h>
#include <unistd.h>

using namespace fcitx;

FetchThread::FetchThread(CloudPinyin *cloudPinyin) : cloudPinyin_(cloudPinyin) {
    curlm_ = curl_multi_init();
    curl_multi_setopt(curlm_, CURLMOPT_MAXCONNECTS, MAX_HANDLE);
    curl_multi_setopt(curlm_, CURLMOPT_SOCKETFUNCTION,
                      &FetchThread::curlCallback);
    curl_multi_setopt(curlm_, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(curlm_, CURLMOPT_TIMERFUNCTION,
                      &FetchThread::curlTimerCallback);
    curl_multi_setopt(curlm_, CURLMOPT_TIMERDATA, this);

    thread_ = std::make_unique<std::thread>(&FetchThread::runThread, this);
}

FetchThread::~FetchThread() {
    exit();
    thread_->join();

    while (!workingQueue.empty()) {
        auto *queue = &workingQueue.front();
        workingQueue.pop_front();
        curl_multi_remove_handle(curlm_, queue->curl());
        queue->release();
    }
    while (!pendingQueue.empty()) {
        auto *queue = &pendingQueue.front();
        pendingQueue.pop_front();
        queue->release();
    }
    while (!finishingQueue.empty()) {
        auto *queue = &finishingQueue.front();
        finishingQueue.pop_front();
        queue->release();
    }

    curl_multi_cleanup(curlm_);
}

void FetchThread::runThread(FetchThread *self) { self->run(); }
int FetchThread::curlCallback(CURL *, curl_socket_t s, int action, void *userp,
                              void *) {
    FetchThread *self = static_cast<FetchThread *>(userp);
    self->curl(s, action);

    return 0;
}

void FetchThread::handleIO(int fd, IOEventFlags flags) {
    int mask = 0;
    if (flags & IOEventFlag::In) {
        mask |= CURL_CSELECT_IN;
    }
    if (flags & IOEventFlag::Out) {
        mask |= CURL_CSELECT_OUT;
    }
    if (flags & IOEventFlag::Err) {
        mask |= CURL_CSELECT_ERR;
    }
    int still_running = 0;
    CURLMcode mcode;
    do {
        mcode = curl_multi_socket_action(curlm_, fd, mask, &still_running);
    } while (mcode == CURLM_CALL_MULTI_PERFORM);

    processMessages();
}

void FetchThread::processMessages() {
    int num_messages = 0;
    CURLMsg *curl_message = curl_multi_info_read(curlm_, &num_messages);

    while (curl_message != NULL) {
        if (curl_message->msg == CURLMSG_DONE) {
            int curl_result = curl_message->data.result;
            void *p = nullptr;
            curl_easy_getinfo(curl_message->easy_handle, CURLINFO_PRIVATE, &p);
            auto *queue = static_cast<CurlQueue *>(p);
            curl_multi_remove_handle(curlm_, queue->curl());
            queue->finish(curl_result);
            queue->remove();
            finished(queue);
        }
        curl_message = curl_multi_info_read(curlm_, &num_messages);
    }
}

void FetchThread::curl(curl_socket_t s, int action) {
    // if loop is gone, don't bother do anything
    if (!loop_) {
        return;
    }
    if (action == CURL_POLL_REMOVE) {
        events_.erase(s);
    } else {
        auto iter = events_.find(s);
        if (iter == events_.end()) {
            auto *that_ = this;
            auto p = events_.emplace(
                s, loop_->addIOEvent(
                       s, IOEventFlags(0),
                       [that_](EventSourceIO *, int fd, IOEventFlags flags) {
                           auto *that = that_;
                           // make sure "that" is valid since io handler may
                           // free itself.
                           that->handleIO(fd, flags);
                           return true;
                       }));
            iter = p.first;
        }
        IOEventFlags flags(0);
        if (action == CURL_POLL_IN) {
            flags = IOEventFlag::In;
        } else if (action == CURL_POLL_OUT) {
            flags = IOEventFlag::Out;
        } else if (action == CURL_POLL_INOUT) {
            flags |= IOEventFlag::In;
            flags |= IOEventFlag::Out;
        }

        iter->second->setEvents(flags);
    }
}

int FetchThread::curlTimerCallback(CURLM *, long timeout_ms, void *user) {
    auto *self = static_cast<FetchThread *>(user);
    self->curlTimer(timeout_ms);
    return 0;
}

void FetchThread::curlTimer(long timeout_ms) {
    if (!loop_) {
        return;
    }
    if (!timer_) {
        timer_ = loop_->addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + timeout_ms * 1000, 0,
            [this](EventSourceTime *, uint64_t) {
                CURLMcode mcode;
                int still_running;
                do {
                    mcode = curl_multi_socket_action(
                        curlm_, CURL_SOCKET_TIMEOUT, 0, &still_running);
                } while (mcode == CURLM_CALL_MULTI_PERFORM);
                processMessages();
                return true;
            });
        timer_->setOneShot();
    } else {
        timer_->setNextInterval(timeout_ms * 1000);
        timer_->setOneShot();
    }
}

void FetchThread::finished(CurlQueue *queue) {
    {
        std::lock_guard<std::mutex> lock(finishQueueLock);
        finishingQueue.push_back(*queue);
    }
    cloudPinyin_->notifyFinished();
}

bool FetchThread::addRequest(const SetupRequestCallback &callback) {
    CurlQueue *queue = nullptr;
    for (auto &handle : handles_) {
        if (!handle.busy()) {
            queue = &handle;
            break;
        }
    }
    if (!queue) {
        return false;
    }
    callback(queue);

    {
        std::lock_guard<std::mutex> lock(pendingQueueLock);
        pendingQueue.push_back(*queue);
    }

    // Handle pending queue in fetch thread.
    dispatcher_.schedule([this]() {
        std::lock_guard<std::mutex> lock(pendingQueueLock);

        while (!pendingQueue.empty()) {
            auto *queue = &pendingQueue.front();
            pendingQueue.pop_front();
            curl_multi_add_handle(curlm_, queue->curl());
            workingQueue.push_back(*queue);
        }
    });
    return true;
}

void FetchThread::exit() {
    dispatcher_.schedule([this]() {
        loop_->exit();
        dispatcher_.detach();
    });
}

CurlQueue *FetchThread::popFinished() {
    std::lock_guard<std::mutex> lock(finishQueueLock);
    CurlQueue *result = nullptr;
    if (!finishingQueue.empty()) {
        result = &finishingQueue.front();
        finishingQueue.pop_front();
    }
    return result;
}

void FetchThread::run() {
    loop_.reset(new fcitx::EventLoop);

    dispatcher_.attach(loop_.get());
    loop_->exec();
    // free events ahead of time
    timer_.reset();
    events_.clear();
    loop_.reset();
}
