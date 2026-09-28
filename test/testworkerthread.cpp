/*
 * SPDX-FileCopyrightText: 2026-2026 luojiyin <luojiyin@hotmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "../im/pinyin/workerthread.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/log.h>
#include <future>
#include <memory>
#include <thread>
#include <vector>

using namespace fcitx;

namespace {

constexpr auto idleTimeout = std::chrono::milliseconds(100);

template <typename Predicate>
bool waitUntil(Predicate predicate) {
    for (int i = 0; i < 2000; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

} // namespace

int main() {
    EventLoop loop;
    EventDispatcher dispatcher;
    dispatcher.attach(&loop);

    WorkerThread worker(dispatcher, idleTimeout);
    // The thread should not be started until there is a task.
    FCITX_ASSERT(!worker.running());

    std::atomic<int> counter = 0;
    std::vector<std::unique_ptr<TaskToken>> tokens;
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();

    // Keep the worker busy so that it is guaranteed to be running while we
    // check its state.
    tokens.push_back(
        worker.addTask(std::packaged_task<int()>([&counter, releaseFuture]() {
                           counter++;
                           releaseFuture.wait();
                           return 0;
                       }),
                       [](std::shared_future<int> &) {}));
    FCITX_ASSERT(waitUntil([&counter]() { return counter.load() == 1; }));
    FCITX_ASSERT(worker.running());

    // The thread stops itself once it becomes idle.
    release.set_value();
    FCITX_ASSERT(waitUntil([&worker]() { return !worker.running(); }));

    // Tasks queued while the worker is busy must all run before the thread
    // can stop again.
    std::promise<void> burstRelease;
    auto burstReleaseFuture = burstRelease.get_future().share();
    std::atomic<int> burstCounter = 0;
    constexpr int burstSize = 8;
    tokens.push_back(worker.addTask(
        std::packaged_task<int()>([&burstCounter, burstReleaseFuture]() {
            burstCounter++;
            burstReleaseFuture.wait();
            return 0;
        }),
        [](std::shared_future<int> &) {}));
    FCITX_ASSERT(
        waitUntil([&burstCounter]() { return burstCounter.load() == 1; }));
    for (int i = 1; i < burstSize; i++) {
        tokens.push_back(
            worker.addTask(std::packaged_task<int()>([&burstCounter]() {
                               burstCounter++;
                               return 0;
                           }),
                           [](std::shared_future<int> &) {}));
    }
    burstRelease.set_value();
    FCITX_ASSERT(waitUntil(
        [&burstCounter]() { return burstCounter.load() == burstSize; }));
    FCITX_ASSERT(waitUntil([&worker]() { return !worker.running(); }));

    // The thread must be restarted for new tasks, and the future must be
    // delivered back through the event dispatcher while the token is alive.
    std::atomic<bool> callbackCalled = false;
    tokens.push_back(worker.addTask(
        std::packaged_task<int()>([&counter]() {
            counter++;
            return 42;
        }),
        [&callbackCalled, &loop](std::shared_future<int> &future) {
            FCITX_ASSERT(future.get() == 42);
            callbackCalled = true;
            loop.exit();
        }));
    FCITX_ASSERT(waitUntil([&counter]() { return counter.load() == 2; }));

    // Safety net so that the test cannot hang if the callback is never
    // delivered.
    auto timeout =
        loop.addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 2000000, 0,
                          [&loop](EventSourceTime *, uint64_t) {
                              loop.exit();
                              return false;
                          });
    FCITX_ASSERT(loop.exec());
    FCITX_ASSERT(callbackCalled);

    return 0;
}
