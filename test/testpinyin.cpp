/*
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <algorithm>
#include <cstdint>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <memory>
#include <string_view>
#include <utility>

using namespace fcitx;

std::unique_ptr<EventSourceTime> endTestEvent;
void testPunctuationPart2(Instance *instance);

int findCandidate(InputContext *ic, std::string_view word) {
    auto candList = ic->inputPanel().candidateList();
    for (int i = 0; i < candList->toBulk()->totalSize(); i++) {
        const auto &candidate = candList->toBulk()->candidateFromAll(i);
        if (candidate.text().toString() == word) {
            return i;
        }
    }
    return -1;
}

int findCandidateOrDie(InputContext *ic, std::string_view word) {
    auto index = findCandidate(ic, word);
    FCITX_ASSERT(index >= 0) << "Failed to find candidate " << word;
    return index;
}

void findAndSelectCandidate(InputContext *ic, std::string_view word) {
    auto candList = ic->inputPanel().candidateList();
    candList->candidate(findCandidateOrDie(ic, word)).select(ic);
}

void sendControlSpace(AddonInstance *testfrontend, InputContext *ic) {
    for (int i = 0; i < 2; i++) {
        testfrontend->call<ITestFrontend::keyEvent>(ic->uuid(),
                                                    Key("Control_L"), false);
        testfrontend->call<ITestFrontend::keyEvent>(
            ic->uuid(), Key("Control+space"), false);
        testfrontend->call<ITestFrontend::keyEvent>(ic->uuid(),
                                                    Key("Control+space"), true);
        testfrontend->call<ITestFrontend::keyEvent>(
            ic->uuid(), Key("Control+Control_L"), true);
        ic->reset();
    }
}

void setup(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin", true);
        FCITX_ASSERT(pinyin);
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("pinyin"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("shuangpin"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(std::move(defaultGroup));
    });
}

void testBasic(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("俺");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ni");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("ni");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("你hao");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

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
        // Make a partial selection, we do search because the data might change.
        findAndSelectCandidate(ic, "你");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

        // Test switch input method.
        testfrontend->call<ITestFrontend::pushCommitExpectation>("nihao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        sendControlSpace(testfrontend, ic);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("你hao");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        // Make a partial selection, we do search because the data might change.
        findAndSelectCandidate(ic, "你");
        sendControlSpace(testfrontend, ic);

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
        sendControlSpace(testfrontend, ic);

        config.setValueByPath("SwitchInputMethodBehavior", "Clear");
        pinyin->setConfig(config);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        sendControlSpace(testfrontend, ic);
    });
}

void testSelectByChar(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("你好主病");
        findAndSelectCandidate(ic, "你好");
        auto candidateIdx = findCandidateOrDie(ic, "公主");
        ic->inputPanel().candidateList()->toBulkCursor()->setGlobalCursorIndex(
            candidateIdx);
        // With default config, this should select "主".
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("]"), false);
        findAndSelectCandidate(ic, "病");
    });
}

void testUppercase(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("Apple");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("A"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

        testfrontend->call<ITestFrontend::pushCommitExpectation>("iPhone");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("P"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
    });
}

void testForget(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        auto *candidateList = ic->inputPanel().candidateList().get();
        const auto &cand = candidateList->candidate(0);
        auto *actionable = candidateList->toActionable();
        FCITX_ASSERT(actionable);
        FCITX_ASSERT(actionable->hasAction(cand));
        auto actions = actionable->candidateActions(cand);
        FCITX_ASSERT(!actions.empty());
        FCITX_ASSERT(actions[0].id() == 0);
        actionable->triggerAction(cand, 0);
        FCITX_ASSERT(ic->inputPanel().candidateList());
    });
}

void testActionInStrokeFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        // Target ppp for 彡
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        auto *candidateList = ic->inputPanel().candidateList().get();
        findCandidateOrDie(ic, "彡");
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("`"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        candidateList = ic->inputPanel().candidateList().get();
        FCITX_ASSERT(findCandidate(ic, "彡") < 0);
        int index = findCandidateOrDie(ic, "䫠");
        const auto &cand = candidateList->candidate(index);
        auto *actionable = candidateList->toActionable();
        FCITX_ASSERT(actionable);
        FCITX_ASSERT(actionable->hasAction(cand));
        auto actions = actionable->candidateActions(cand);
        FCITX_ASSERT(!actions.empty());
        FCITX_ASSERT(actions[0].id() == 0);
        actionable->triggerAction(cand, 0);
        FCITX_ASSERT(ic->inputPanel().candidateList());
    });
}

void testPin(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("y"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        auto index1 = findCandidateOrDie(ic, "同音");
        auto index2 = findCandidateOrDie(ic, "痛饮");
        const auto oldIndex1 = index1;
        const auto oldIndex2 = index2;
        FCITX_INFO() << "同音:" << index1 << " "
                     << "痛饮:" << index2;
        {
            auto *candidateList = ic->inputPanel().candidateList().get();
            // Pin the one that is after.
            const auto &cand =
                candidateList->candidate(std::max(index1, index2));
            auto *actionable = candidateList->toActionable();
            FCITX_ASSERT(actionable);
            FCITX_ASSERT(actionable->hasAction(cand));
            auto actions = actionable->candidateActions(cand);
            FCITX_ASSERT(!actions.empty());
            FCITX_ASSERT(
                std::any_of(actions.begin(), actions.end(),
                            [](const auto &a) { return a.id() == 1; }));
            FCITX_ASSERT(
                !std::any_of(actions.begin(), actions.end(),
                             [](const auto &a) { return a.id() == 2; }));
            // This is pin action, 痛饮 should be pined to head.
            actionable->triggerAction(cand, 1);
        }
        index1 = findCandidateOrDie(ic, "同音");
        index2 = findCandidateOrDie(ic, "痛饮");
        FCITX_INFO() << "同音:" << index1 << " "
                     << "痛饮:" << index2;
        FCITX_ASSERT(index1 != oldIndex1);
        FCITX_ASSERT(index2 != oldIndex2);
        FCITX_ASSERT(std::min(index1, index2) == 0);

        {
            auto *candidateList = ic->inputPanel().candidateList().get();
            const auto &candNew = candidateList->candidate(index2);
            auto *actionable = candidateList->toActionable();
            FCITX_ASSERT(actionable);
            FCITX_ASSERT(actionable->hasAction(candNew));
            auto actions = actionable->candidateActions(candNew);
            FCITX_ASSERT(!actions.empty());
            // Check if deletable action is there.
            FCITX_ASSERT(
                std::any_of(actions.begin(), actions.end(),
                            [](const auto &a) { return a.id() == 2; }));
            // This is delete custom phrase action, 痛饮 should be pined to
            // head.
            actionable->triggerAction(candNew, 2);
        }
        index1 = findCandidateOrDie(ic, "同音");
        index2 = findCandidateOrDie(ic, "痛饮");
        FCITX_INFO() << "同音:" << index1 << " "
                     << "痛饮:" << index2;
        FCITX_ASSERT(index1 == oldIndex1);
        FCITX_ASSERT(index2 == oldIndex2);
    });
}

void testQuickPhraseTrigger(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "www.");

        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "bbs.");

        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("r"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("@"), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "user@");

        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(":"), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "http:");

        ic->reset();
        // htt: shouldn't trigger.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

        FCITX_ASSERT(ic->inputPanel().candidateList());
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
        const auto firstCandidate =
            ic->inputPanel().candidateList()->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            firstCandidate);
        testfrontend->call<ITestFrontend::pushCommitExpectation>("：");

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key(":"), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "");
    });
}

void testVQuickPhraseTrigger(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");

        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("v"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "v.");

        instance->setCurrentInputMethod(ic, "shuangpin", true);
        ic->reset();
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("V"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("."), false);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "V.");
    });
}

void testPunctuation(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

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
            [instance](EventSourceTime *event, uint64_t) {
                testPunctuationPart2(instance);
                delete event;
                return true;
            });
        (void)event.release();
    });
}

void testPunctuationPart2(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

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
        TESTING_BINARY_DIR, {"bin"},
        {TESTING_BINARY_DIR "/test", TESTING_BINARY_DIR "/im",
         TESTING_BINARY_DIR "/modules", TESTING_SOURCE_DIR "/modules",
         StandardPaths::fcitxPath("pkgdatadir")});
    // fcitx::Log::setLogRule("default=5,table=5,libime-table=5");
    char arg0[] = "testpinyin";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,pinyin,punctuation,"
                  "pinyinhelper,spell,quickphrase";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,pinyin=5");
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    setup(&instance);
    testBasic(&instance);
    testSelectByChar(&instance);
    testUppercase(&instance);
    testForget(&instance);
    testActionInStrokeFilter(&instance);
    testPin(&instance);
    testQuickPhraseTrigger(&instance);
    testVQuickPhraseTrigger(&instance);
    testPunctuation(&instance);
    instance.exec();
    endTestEvent.reset();
    return 0;
}
