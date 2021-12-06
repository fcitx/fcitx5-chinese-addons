/*
 * SPDX-FileCopyrightText: 2020-2020 Vifly <viflythink@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "config.h"
#include "testdir.h"
#include "testfrontend_public.h"
#include "testim_public.h"
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/testing.h>
#include <fcitx/action.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>
#include <iostream>
#include <thread>

using namespace fcitx;

std::string getTestWord(const std::string &s) {
    std::string result;
    if (s == "a") {
        result = "无";
    } else if (s == "b") {
        result = "测";
    } else if (s == "c") {
        result = "多个词";
    } else if (s == "d") {
        result = "书";
    } else if (s == "e") {
        result = "時";
    } else if (s == "f") {
        result = "皇后";
    } else if (s == "g") {
        result = "启动";
    } else {
        result = s;
    }
    return result;
}

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([dispatcher, instance]() {
        auto *chttrans = instance->addonManager().addon("chttrans", true);
        FCITX_ASSERT(chttrans);
        auto testfrontend = instance->addonManager().addon("testfrontend");
        auto testim = instance->addonManager().addon("testim");
        auto *action =
            instance->userInterfaceManager().lookupAction("chttrans");

        instance->inputMethodManager().addEmptyGroup("test");
        instance->inputMethodManager().setCurrentGroup("test");
        auto inputmethodgroup = InputMethodGroup("test");
        inputmethodgroup.inputMethodList().emplace_back("sim");
        inputmethodgroup.inputMethodList().emplace_back("trad");
        inputmethodgroup.setDefaultInputMethod("sim");
        instance->inputMethodManager().setGroup(inputmethodgroup);

        FCITX_INFO() << instance->inputMethodManager()
                            .currentGroup()
                            .defaultInputMethod();

        testim->call<ITestIM::setHandler>([action](const InputMethodEntry &,
                                                   KeyEvent &keyEvent) {
            if (keyEvent.key().states() != KeyState::NoState ||
                keyEvent.isRelease()) {
                return;
            }

            auto s = Key::keySymToUTF8(keyEvent.key().sym());
            if (!s.empty()) {
                if (!action->isParent(&keyEvent.inputContext()->statusArea())) {
                    keyEvent.inputContext()->statusArea().addAction(
                        StatusGroup::InputMethod, action);
                    action->activate(keyEvent.inputContext());
                }

                // Test convertTradToSimp
                std::string word = getTestWord(s);
                keyEvent.inputContext()->commitString(word);
                keyEvent.filterAndAccept();
            }
        });

        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        RawConfig config;
        config.setValueByPath("Engine", "OpenCC");
        config.setValueByPath("Hotkey/0", "Control+Shift+F");
        chttrans->setConfig(config);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("無");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("測");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("多個詞");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

#ifdef ENABLE_OPENCC
        testfrontend->call<ITestFrontend::pushCommitExpectation>("皇后");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("啓動");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);

        config.setValueByPath("OpenCCS2TProfile", "s2tw.json");
        chttrans->setConfig(config);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("啟動");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
#endif

        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key("Control+Shift+F"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("书");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);

        // Switch to Trad IM from Sim IM
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("时");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);

        // Test Native engine
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        config.setValueByPath("Engine", "Native");
        FCITX_INFO() << config;
        chttrans->setConfig(config);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("皇後");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("皇后");
        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key("Control+Shift+F"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);

        dispatcher->detach();
        instance->exit();
    });
}

void runInstance() {}

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR,
        {"modules/chttrans", StandardPath::fcitxPath("addondir")},
        {"test", TESTING_SOURCE_DIR "/modules"});

    fcitx::Log::setLogRule("*=5");

    char arg0[] = "testchttrans";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,chttrans";
    char *argv[] = {arg0, arg1, arg2};
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    std::thread thread(scheduleEvent, &dispatcher, &instance);
    instance.exec();
    thread.join();

    return 0;
}
