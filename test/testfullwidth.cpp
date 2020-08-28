//
// Created by Vifly on 8/23/20.
//

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

        dispatcher->detach();
        instance->exit();
    });
}

void runInstance() {}

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR,
        {"src/addonloader", StandardPath::fcitxPath("addondir")},
        {"test", TESTING_SOURCE_DIR "/test",
         StandardPath::fcitxPath("pkgdatadir", "testing")});
    fcitx::Log::setLogRule("*=5");

    Instance instance(0, nullptr);
    instance.addonManager().registerDefaultLoader(nullptr);
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    std::thread thread(scheduleEvent, &dispatcher, &instance);
    instance.exec();
    thread.join();

    return 0;
}