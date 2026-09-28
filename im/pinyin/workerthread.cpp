/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "workerthread.h"
#include <chrono>
#include <condition_variable>
#include <fcitx-utils/eventdispatcher.h>
#include <memory>
#include <mutex>
#include <thread>

WorkerThread::WorkerThread(fcitx::EventDispatcher &dispatcher,
                           std::chrono::milliseconds idleTimeout)
    : dispatcher_(dispatcher), idleTimeout_(idleTimeout) {}

WorkerThread::~WorkerThread() {
    // Unlike other thread, there is no need to use a event loop  since there is
    // no IO monitoring, simply use exit_ to notify the exit.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        exit_ = true;
        condition_.notify_one();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void WorkerThread::startThread() {
    if (running_) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    thread_ = std::thread(&WorkerThread::runThread, this);
    running_ = true;
}

std::unique_ptr<TaskToken>
WorkerThread::addTaskImpl(std::function<void()> task,
                          std::function<void()> onDone) {
    // Return an empty TrackableObject, so the unneeded task can be thrown away
    // by simply delete TaskToken.
    auto token = std::make_unique<TaskToken>();
    std::lock_guard<std::mutex> lock(mutex_);
    startThread();
    queue_.push({.task = std::move(task),
                 .callback = std::move(onDone),
                 .context = token->watch()});
    condition_.notify_one();
    return token;
}

void WorkerThread::run() {
    while (true) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            if (!condition_.wait_for(lock, idleTimeout_, [this] {
                    return exit_ || !queue_.empty();
                })) {
                // Nothing to do for a while, stop the thread. It will be
                // started again when a new task comes in.
                running_ = false;
                break;
            }
            if (exit_) {
                break;
            }

            task = std::move(queue_.front());
            queue_.pop();
        }
        // Run the actual task.
        task.task();
        dispatcher_.scheduleWithContext(std::move(task.context),
                                        std::move(task.callback));
    }
}
