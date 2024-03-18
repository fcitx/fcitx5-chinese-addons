/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <memory>

using namespace fcitx;

std::unique_ptr<EventSourceTime> endTestEvent;
void testPunctuationPart2(EventDispatcher *dispatcher, Instance *instance);

void testBasic(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        FCITX_ASSERT(pinyin);
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("pinyin"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(std::move(defaultGroup));
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("俺");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ni");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ni");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("你hao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key(FcitxKey_BackSpace), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("KP_Enter"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        auto ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        // Make a partial selection, we do search because the data might change.
        auto candList = ic->inputPanel().candidateList();
        for (int i = 0; i < candList->size(); i++) {
            auto &candidate = candList->candidate(i);
            if (candidate.text().toString() == "你") {
                candidate.select(ic);
                break;
            }
        }
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

        // Test switch input method.
        testfrontend->call<ITestFrontend::pushCommitExpectation>("nihao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("你hao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        // Make a partial selection, we do search because the data might change.
        candList = ic->inputPanel().candidateList();
        for (int i = 0; i < candList->size(); i++) {
            auto &candidate = candList->candidate(i);
            if (candidate.text().toString() == "你") {
                candidate.select(ic);
                break;
            }
        }
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);

        RawConfig config;
        config.setValueByPath("SwitchInputMethodBehavior",
                              "Commit default selection");
        pinyin->setConfig(config);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        auto sentence =
            ic->inputPanel().candidateList()->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(sentence);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);

        config.setValueByPath("SwitchInputMethodBehavior", "Clear");
        pinyin->setConfig(config);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
    });
}

void testPunctuation(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([instance, dispatcher]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("。");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        // This is cancel last eng.
        testfrontend->call<ITestFrontend::pushCommitExpectation>("。");
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("BackSpace"), false));

        auto event = instance->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 2000000, 0,
            [dispatcher, instance](EventSourceTime *event, uint64_t) {
                testPunctuationPart2(dispatcher, instance);
                delete event;
                return true;
            });
        (void)event.release();
    });
}

void testPunctuationPart2(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        // This should not cancel last eng.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("BackSpace"), false));

        endTestEvent = instance->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 2000000, 0,
            [instance](EventSourceTime *, uint64_t) {
                instance->exit();
                return true;
            });
    });
}

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR,
        {TESTING_BINARY_DIR "/modules/pinyinhelper",
         TESTING_BINARY_DIR "/modules/punctuation",
         TESTING_BINARY_DIR "/im/pinyin"},
        {TESTING_BINARY_DIR "/test", TESTING_BINARY_DIR "/im",
         TESTING_BINARY_DIR "/modules", TESTING_SOURCE_DIR "/modules",
         StandardPath::fcitxPath("pkgdatadir")});
    // fcitx::Log::setLogRule("default=5,table=5,libime-table=5");
    char arg0[] = "testpinyin";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,pinyin,punctuation,"
                  "pinyinhelper";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,pinyin=5");
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    testBasic(&dispatcher, &instance);
    testPunctuation(&dispatcher, &instance);
    instance.exec();
    endTestEvent.reset();
    return 0;
}
