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
#include "fetch.h"
#include <fcitx-utils/event.h>
#include <fcitx-utils/fs.h>
#include <fcntl.h>
#include <unistd.h>

using namespace fcitx;

FetchThread::FetchThread(fcitx::UnixFD notifyFd)
    : notifyFd_(std::move(notifyFd)) {
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK) < 0) {
        throw std::runtime_error("Failed to create pipe");
    }
    selfPipeFd_[0].give(pipefd[0]);
    selfPipeFd_[1].give(pipefd[1]);

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
    quit();
    thread_->join();

    while (workingQueue.size()) {
        auto queue = &workingQueue.front();
        workingQueue.pop_front();
        curl_multi_remove_handle(curlm_, queue->curl());
        queue->release();
    }
    while (pendingQueue.size()) {
        auto queue = &pendingQueue.front();
        pendingQueue.pop_front();
        queue->release();
    }
    while (finishingQueue.size()) {
        auto queue = &finishingQueue.front();
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
            auto queue = static_cast<CurlQueue *>(p);
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
            auto that_ = this;
            auto p = events_.emplace(
                s, loop_->addIOEvent(
                       s, IOEventFlags(0),
                       [that_](EventSourceIO *, int fd, IOEventFlags flags) {
                           auto that = that_;
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
    auto self = static_cast<FetchThread *>(user);
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
    std::lock_guard<std::mutex> lock(finishQueueLock);

    finishingQueue.push_back(*queue);

    char c = 0;
    fs::safeWrite(notifyFd_.fd(), &c, sizeof(char));
}

bool FetchThread::addRequest(SetupRequestCallback callback) {
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

    std::lock_guard<std::mutex> lock(pendingQueueLock);
    pendingQueue.push_back(*queue);

    char c = 0;
    fs::safeWrite(selfPipeFd_[1].fd(), &c, sizeof(c));
    return true;
}

void FetchThread::quit() {
    char c = 1;
    fs::safeWrite(selfPipeFd_[1].fd(), &c, sizeof(c));
}

CurlQueue *FetchThread::popFinished() {
    std::lock_guard<std::mutex> lock(finishQueueLock);
    CurlQueue *result = nullptr;
    if (finishingQueue.size()) {
        result = &finishingQueue.front();
        finishingQueue.pop_front();
    }
    return result;
}

void FetchThread::run() {
    loop_.reset(new fcitx::EventLoop);
    std::unique_ptr<EventSourceIO> event(loop_->addIOEvent(
        selfPipeFd_[0].fd(), IOEventFlag::In,
        [this](EventSourceIO *, int, IOEventFlags) {
            char c;
            int r = 0;
            bool endflag = false;
            while ((r = fs::safeRead(selfPipeFd_[0].fd(), &c, sizeof(char))) >
                   0) {
                if (c == 1) {
                    endflag = true;
                }
            }
            if (r == 0 || endflag) {
                loop_->quit();
                return true;
            }

            std::lock_guard<std::mutex> lock(pendingQueueLock);

            while (pendingQueue.size()) {
                auto queue = &pendingQueue.front();
                pendingQueue.pop_front();
                curl_multi_add_handle(curlm_, queue->curl());
                workingQueue.push_back(*queue);
            }

            return true;
        }));

    loop_->exec();
    // free events ahead of time
    timer_.reset();
    events_.clear();
    loop_.reset();
}
