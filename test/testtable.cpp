/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterface.h>
#include <utility>

using namespace fcitx;

int findCandidateOrDie(InputContext *ic, std::string_view word) {
    auto candList = ic->inputPanel().candidateList();
    for (int i = 0; i < candList->toBulk()->totalSize(); i++) {
        const auto &candidate = candList->toBulk()->candidateFromAll(i);
        if (candidate.text().toString() == word) {
            return i;
        }
    }
    FCITX_ASSERT(false) << "Failed to find candidate: " << word;
    return -1;
}

void scheduleEvent(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *table = instance->addonManager().addon("table", true);
        FCITX_ASSERT(table);
    });
    instance->eventDispatcher().schedule([instance]() {
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(InputMethodGroupItem("erbi"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(std::move(defaultGroup));
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
        auto *ic = instance->inputContextManager().findByUUID(uuid);
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
        // Test auto quickphrase
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("A"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
    });
    instance->eventDispatcher().schedule([instance]() {
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(InputMethodGroupItem("wbx"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(std::move(defaultGroup));
        auto *table = instance->addonManager().addon("table", true);
        RawConfig config;
        const auto *wbxConfig =
            reinterpret_cast<InputMethodEngine *>(table)
                ->getConfigForInputMethod(
                    *instance->inputMethodManager().entry("wbx"));
        wbxConfig->save(config);
        config.setValueByPath("AutoPhraseWithPhrase", "True");
        reinterpret_cast<InputMethodEngine *>(table)->setConfigForInputMethod(
            *instance->inputMethodManager().entry("wbx"), config);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("工");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("工");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("工工");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("工工工工");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                    false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        auto *ic = instance->inputContextManager().findByUUID(uuid);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                true);
        // This a trigger auto commit.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        // Select 工工
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);

        // Auto phrase should learn 工工工工
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("5"), false);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

        auto *candidateList = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(candidateList);
        auto idx = findCandidateOrDie(ic, "工工工工");
        auto *actionable = ic->inputPanel().candidateList()->toActionable();
        FCITX_ASSERT(actionable);
        FCITX_ASSERT(actionable->hasAction(candidateList->candidate(idx)));
        FCITX_ASSERT(
            actionable->candidateActions(candidateList->candidate(idx))[0]
                .id() == 0);
        actionable->triggerAction(candidateList->candidate(idx), 0);

        // Check if 工工工工 is deleted.
        candidateList = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(candidateList);
        for (int i = 0; i < candidateList->size(); i++) {
            FCITX_ASSERT(candidateList->candidate(i).text().toString() !=
                         "工工工工")
                << "Candidate " << i << " is not deleted.";
        }
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel,
                                true);
    });
    instance->eventDispatcher().schedule([instance]() { instance->exit(); });
}

int main() {
    setupTestingEnvironment(TESTING_BINARY_DIR,
                            {TESTING_BINARY_DIR "/modules/pinyinhelper",
                             TESTING_BINARY_DIR "/modules/punctuation",
                             TESTING_BINARY_DIR "/im/table"},
                            {TESTING_BINARY_DIR "/test",
                             TESTING_BINARY_DIR "/modules",
                             StandardPath::fcitxPath("pkgdatadir")});
    fcitx::Log::setLogRule("default=5,table=5,libime-table=5");
    char arg0[] = "testtable";
    char arg1[] = "--disable=all";
    char arg2[] =
        "--enable=testui,testim,testfrontend,table,quickphrase,punctuation,"
        "pinyinhelper";
    char *argv[] = {arg0, arg1, arg2};
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    scheduleEvent(&instance);
    instance.exec();

    return 0;
}
