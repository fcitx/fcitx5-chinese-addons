/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <iostream>

using namespace fcitx;

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([instance]() {
        auto *table = instance->addonManager().addon("table", true);
        FCITX_ASSERT(table);
    });
    dispatcher->schedule([dispatcher, instance]() {
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(InputMethodGroupItem("erbi"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(defaultGroup);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("萌");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("豚");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("萌豚");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("萌豚");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("mbA");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);
        auto ic = instance->inputContextManager().findByUUID(uuid);
        // Check no candidate.
        FCITX_ASSERT(!ic->inputPanel().candidateList());
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Escape),
                                                    false);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                true);
        // This t trigger auto commit.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        // This comma trigger only match commit.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(","), false);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);
        // Auto phrase does not do auto select.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        // This trigger auto select because it's now user phrase.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("A"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

        dispatcher->schedule([dispatcher, instance]() {
            dispatcher->detach();
            instance->exit();
        });
    });
}

void runInstance() {}

int main() {
    setupTestingEnvironment(TESTING_BINARY_DIR,
                            {TESTING_BINARY_DIR "/modules/pinyinhelper",
                             TESTING_BINARY_DIR "/modules/punctuation",
                             TESTING_BINARY_DIR "/im/table"},
                            {TESTING_BINARY_DIR "/test",
                             TESTING_BINARY_DIR "/modules",
                             StandardPath::fcitxPath("pkgdatadir")});
    // fcitx::Log::setLogRule("default=5,table=5,libime-table=5");
    char arg0[] = "testtable";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,table,quickphrase,punctuation,"
                  "pinyinhelper";
    char *argv[] = {arg0, arg1, arg2};
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    scheduleEvent(&dispatcher, &instance);
    instance.exec();

    return 0;
}
