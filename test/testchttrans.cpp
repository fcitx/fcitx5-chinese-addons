/*
 * SPDX-FileCopyrightText: 2020-2020 Vifly <viflythink@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
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

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([instance]() {
        auto *chttrans = instance->addonManager().addon("chttrans", true);
        FCITX_ASSERT(chttrans);
    });

    dispatcher->schedule([dispatcher, instance]() {
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

                // Test words and convertTradToSimp
                if (s == "多") {
                    s = "多个词";
                } else if (s == "時") {
                    action->activate(keyEvent.inputContext());
                }

                keyEvent.inputContext()->commitString(s);
                keyEvent.filterAndAccept();
            }
        });

        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        testfrontend->call<ITestFrontend::pushCommitExpectation>("無");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("无"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("測");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("测"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("多個詞");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("多"), false);

        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key("Control+Shift+F"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("书");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("书"), false);

        // Switch to Trad IM from Sim IM
        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key("Control+Shift+F"), false);
        inputmethodgroup.setDefaultInputMethod("trad");
        instance->inputMethodManager().setGroup(inputmethodgroup);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("时");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("時"), false);

        dispatcher->detach();
        instance->exit();
    });
}

void runInstance() {}

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR,
        {"modules/chttrans", StandardPath::fcitxPath("addondir")}, {"test"});

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