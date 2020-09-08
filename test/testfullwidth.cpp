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
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>
#include <iostream>
#include <thread>

using namespace fcitx;

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([instance]() {
        auto *fullwidth = instance->addonManager().addon("fullwidth", true);
        FCITX_ASSERT(fullwidth);
        RawConfig config;
        config.setValueByPath("Hotkey/0", "Control+period");
        fullwidth->setConfig(config);
    });

    dispatcher->schedule([dispatcher, instance]() {
        auto testfrontend = instance->addonManager().addon("testfrontend");
        auto testim = instance->addonManager().addon("testim");
        auto *action =
            instance->userInterfaceManager().lookupAction("fullwidth");

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

                // Test multiple character string
                if (s == "c") {
                    s = "abcd";
                } else if (s == "d") {
                    s = "test!";
                }
                keyEvent.inputContext()->commitString(s);
                keyEvent.filterAndAccept();
            }
        });

        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        testfrontend->call<ITestFrontend::pushCommitExpectation>("ａ");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ｂ");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("～");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("~"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("？");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("?"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("ａｂｃｄ");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ｔｅｓｔ！");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);

        // Test toggle key
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+period"),
                                                    false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("e");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::pushCommitExpectation>(",");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(","), false);

        dispatcher->detach();
        instance->exit();
    });
}

void runInstance() {}

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR,
        {"modules/fullwidth", StandardPath::fcitxPath("addondir")}, {"test"});
    fcitx::Log::setLogRule("*=5");

    char arg0[] = "testfullwidth";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,fullwidth";
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