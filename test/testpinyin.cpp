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
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidateaction.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace fcitx;

namespace {

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

std::vector<CandidateAction> tabActions(InputContext *ic) {
    auto candidateList = ic->inputPanel().candidateList();
    FCITX_ASSERT(candidateList);
    auto *tabbed = candidateList->toTabbed();
    FCITX_ASSERT(tabbed);
    auto span = tabbed->tabActions();
    return {span.begin(), span.end()};
}

std::vector<std::string> tabTexts(InputContext *ic) {
    std::vector<std::string> result;
    for (const auto &action : tabActions(ic)) {
        if (action.id() >= 0) {
            result.push_back(action.text());
        }
    }
    return result;
}

bool hasTab(InputContext *ic, std::string_view text) {
    auto actions = tabActions(ic);
    return std::ranges::find(actions, text, &CandidateAction::text) !=
           actions.end();
}

size_t countTab(InputContext *ic, std::string_view text) {
    auto actions = tabActions(ic);
    return std::ranges::count(actions, text, &CandidateAction::text);
}

bool hasCheckedTab(InputContext *ic) {
    auto actions = tabActions(ic);
    return std::ranges::any_of(actions, [](const auto &action) {
        return action.id() >= 0 && action.isChecked();
    });
}

void clickTab(InputContext *ic, std::string_view text) {
    auto candidateList = ic->inputPanel().candidateList();
    FCITX_ASSERT(candidateList);
    auto *tabbed = candidateList->toTabbed();
    FCITX_ASSERT(tabbed);
    auto actions = tabbed->tabActions();
    auto it = std::ranges::find(actions, text, &CandidateAction::text);
    FCITX_ASSERT(it != actions.end()) << "Tab " << text << " not found";
    tabbed->triggerTabAction(it->id());
}

void clickPreedit(InputContext *ic, int cursor) {
    InvokeActionEvent event(InvokeActionEvent::Action::LeftClick, cursor, ic);
    ic->invokeAction(event);
    FCITX_ASSERT(event.filtered());
}

void typeString(AddonInstance *testfrontend, const ICUUID &uuid,
                std::string_view text) {
    for (char c : text) {
        testfrontend->call<ITestFrontend::keyEvent>(
            uuid, Key(std::string(1, c)), false);
    }
}

TextFormatFlags formatAtByte(const Text &text, size_t offset) {
    size_t pos = 0;
    for (size_t i = 0; i < text.size(); i++) {
        pos += text.stringAt(i).size();
        if (offset < pos) {
            return text.formatAt(i);
        }
    }
    return TextFormatFlag::NoFlag;
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
        testfrontend->call<ITestFrontend::pushCommitExpectation>("呵");
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
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
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

void testPinyinTabFilter(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        findCandidateOrDie(ic, "西安");
        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        auto actionSpan = tabbed->tabActions();
        std::vector<CandidateAction> actions{actionSpan.begin(),
                                             actionSpan.end()};
        std::vector<std::string> names;
        names.reserve(actions.size());
        for (const auto &action : actions) {
            names.push_back(action.text());
        }

        auto indexOf = [&names](std::string_view name) {
            auto it = std::ranges::find(names, name);
            FCITX_ASSERT(it != names.end());
            return std::distance(names.begin(), it);
        };

        const auto xiAction = actions[indexOf("xi")];
        const auto singleAction = actions[indexOf("单字")];
        const auto strokeAction = actions[indexOf("笔画")];

        auto checkedActionsAre = [tabbed](std::initializer_list<int> ids) {
            std::unordered_set<int> checkedIds;
            for (const auto &action : tabbed->tabActions()) {
                if (action.isChecked()) {
                    checkedIds.insert(action.id());
                }
            }
            return checkedIds == std::unordered_set<int>{ids};
        };

        FCITX_ASSERT(actions[0].text() == "xian");
        tabbed->triggerTabAction(actions[0].id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({actions[0].id()}));

        // Trigger same action again should uncheck it.
        tabbed->triggerTabAction(actions[0].id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({}));

        FCITX_ASSERT(xiAction.text() == "xi");
        tabbed->triggerTabAction(xiAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id()}));

        // Pinyin and single char can be checked at the same time.
        FCITX_ASSERT(singleAction.text() == "单字");
        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id(), singleAction.id()}));

        // Trigger single char action again should only uncheck itself.
        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id()}));

        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({xiAction.id(), singleAction.id()}));

        // Trigger pinyin action again should only uncheck itself.
        tabbed->triggerTabAction(xiAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({singleAction.id()}));

        // Stroke action should not uncheck single char action.
        tabbed->triggerTabAction(strokeAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({}));

        actionSpan = tabbed->tabActions();
        actions = {actionSpan.begin(), actionSpan.end()};
        FCITX_ASSERT(actions.size() >= 2);
        FCITX_ASSERT(actions[actions.size() - 2].isSeparator());
        FCITX_ASSERT(actions.back().text() == "返回");
        tabbed->triggerTabAction(actions.back().id());
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);
        FCITX_ASSERT(checkedActionsAre({singleAction.id()}));

        // Trigger single char action again should uncheck it.
        tabbed->triggerTabAction(singleAction.id());
        FCITX_ASSERT(findCandidate(ic, "西安") >= 0);
        FCITX_ASSERT(checkedActionsAre({}));
    });
}

void testPinyinTabFilterWithSeparator(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("'"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("'"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        findAndSelectCandidate(ic, "西");
        auto *tabbed = ic->inputPanel().candidateList()->toTabbed();
        FCITX_ASSERT(tabbed);
        auto actionSpan = tabbed->tabActions();
        FCITX_ASSERT(std::ranges::none_of(actionSpan, [](const auto &a) {
            return a.text().starts_with('\'');
        }));
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
            FCITX_ASSERT(std::ranges::any_of(
                actions, [](const auto &a) { return a.id() == 1; }));
            FCITX_ASSERT(!std::ranges::any_of(
                actions, [](const auto &a) { return a.id() == 2; }));
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
            FCITX_ASSERT(std::ranges::any_of(
                actions, [](const auto &a) { return a.id() == 2; }));
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

void testPunctuationWithCursorAtBeginning(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        ic->setCapabilityFlags(CapabilityFlag::SurroundingText);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        ic->reset();
        ic->surroundingText().setText("text", 0, 0);
        FCITX_ASSERT(ic->surroundingText().isValid());
        // Cursor zero has no preceding character.
        ic->updateSurroundingText();
        testfrontend->call<ITestFrontend::pushCommitExpectation>("。");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("."), false));
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

void testPinyinContinuousSelection(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        RawConfig config;
        config.setValueByPath("Fuzzy/Correction", "QWERTY");
        config.setValueByPath("ContinuousPinyinSelection", "True");
        pinyin->setConfig(config);

        typeString(testfrontend, uuid, "nuhaoxian");
        FCITX_ASSERT(hasTab(ic, "nu"));
        FCITX_ASSERT(hasTab(ic, "ni"));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "nu hao xian");
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 0)
                          .test(TextFormatFlag::HighLight));

        // The fuzzy-corrected syllable is fixed without touching the
        // frontend filter state, and candidates are rebuilt by libime.
        clickTab(ic, "ni");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(!hasTab(ic, "nu"));
        FCITX_ASSERT(hasTab(ic, "hao"));
        FCITX_ASSERT(findCandidate(ic, "你好") >= 0);
        FCITX_ASSERT(findCandidate(ic, "怒") < 0);
        // The fixed syllable is displayed as the corrected pinyin and
        // highlighted.
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xian");
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 2)
                          .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 3)
                          .test(TextFormatFlag::HighLight));

        clickTab(ic, "hao");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(!hasTab(ic, "hao"));
        // Every legal split of the remaining "xian" is offered, not only the
        // reading used by the current candidates.
        FCITX_ASSERT(hasTab(ic, "xian"));
        FCITX_ASSERT(hasTab(ic, "xi"));
        FCITX_ASSERT(findCandidate(ic, "你好先") >= 0);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xian");
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 3)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 6)
                          .test(TextFormatFlag::HighLight));

        // The extra split can also be fixed through libime.
        clickTab(ic, "xi");
        FCITX_ASSERT(!hasTab(ic, "xi"));
        FCITX_ASSERT(hasTab(ic, "an"));
        FCITX_ASSERT(findCandidate(ic, "你好西安") >= 0);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xi an")
            << ic->inputPanel().preedit().toString();
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 3)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 7)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 2)
                          .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 6)
                          .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 9)
                          .test(TextFormatFlag::HighLight));

        // Apostrophes do not consume a syllable. A fix on both sides of one
        // must not make the second syllable available again.
        ic->reset();
        typeString(testfrontend, uuid, "xi'an");
        FCITX_ASSERT(hasTab(ic, "xi"));
        FCITX_ASSERT(!hasTab(ic, "an"));
        clickTab(ic, "xi");
        FCITX_ASSERT(hasTab(ic, "an"));
        clickTab(ic, "an");
        FCITX_ASSERT(!hasTab(ic, "an"));

        // Tabs are ordered by how closely they match the typed input. These
        // are intentional exact-order snapshots of the ranking.
        ic->reset();
        typeString(testfrontend, uuid, "xian");
        FCITX_ASSERT(tabTexts(ic) ==
                     std::vector<std::string>{"xian", "xia", "xi", "x", "xuan",
                                              "zi", "xu", "ci", "X"});

        ic->reset();
        typeString(testfrontend, uuid, "tian");
        FCITX_ASSERT(tabTexts(ic) ==
                     std::vector<std::string>{"tian", "ti", "t", "tuan", "yi",
                                              "tu", "ri", "r", "T"});

        ic->reset();
        typeString(testfrontend, uuid, "fangan");
        FCITX_ASSERT(tabTexts(ic) ==
                     std::vector<std::string>{"fang", "fan", "fa", "f", "gang",
                                              "dang", "dan", "gan", "da", "ga",
                                              "F"});

        // "sh" is parsed as a single incomplete syllable by libime, so its
        // regular segment graph has no node between "s" and "h". Fixing both
        // halves must still work and produce Chinese candidates.
        ic->reset();
        typeString(testfrontend, uuid, "sh");
        FCITX_ASSERT(hasTab(ic, "s"));
        clickTab(ic, "s");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(hasTab(ic, "h"));
        FCITX_ASSERT(findCandidate(ic, "似乎") >= 0);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "s h")
            << ic->inputPanel().preedit().toString();
        clickTab(ic, "h");
        FCITX_ASSERT(!hasTab(ic, "h"));
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(findCandidate(ic, "似乎") >= 0);
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "s h")
            << ic->inputPanel().preedit().toString();
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());

        // addNext in buildContinuousTabActions force-adds single-letter
        // initials so pinyin like "ga" (g + a) can be fixed. libime's
        // parseUserPinyin doesn't produce those splits by itself.
        ic->reset();
        typeString(testfrontend, uuid, "ga");
        FCITX_ASSERT(hasTab(ic, "g"));
        FCITX_ASSERT(hasTab(ic, "ga"));
        clickTab(ic, "g");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(!hasTab(ic, "g"));
        FCITX_ASSERT(hasTab(ic, "a"));
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "g a")
            << ic->inputPanel().preedit().toString();

        // zh/ch/sh initials also get the 0->2 split, so "sha" now exposes
        // both "s" (single-letter) and "sh" (two-letter) initial fixes.
        ic->reset();
        typeString(testfrontend, uuid, "sha");
        FCITX_ASSERT(hasTab(ic, "s"));
        FCITX_ASSERT(hasTab(ic, "sh"));
        FCITX_ASSERT(hasTab(ic, "sha"));
        // The fuzzy "sh" reading of the single-letter "s" split and the real
        // "sh" split both render as "sh". Only the exact split must remain.
        FCITX_ASSERT(countTab(ic, "sh") == 1);
        clickTab(ic, "sh");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(!hasTab(ic, "sh"));
        FCITX_ASSERT(hasTab(ic, "a"));
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "sh a")
            << ic->inputPanel().preedit().toString();

        // Moving the cursor to the start of the composition cancels the fixes
        // from the cursor to the end, so the tab page goes back to the first
        // syllable.
        ic->reset();
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        FCITX_ASSERT(!hasTab(ic, "nu"));
        FCITX_ASSERT(hasTab(ic, "hao"));
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
        FCITX_ASSERT(hasTab(ic, "nu"));
        FCITX_ASSERT(!hasTab(ic, "hao"));

        // Moving to the end keeps the fix because it ends before the cursor.
        clickTab(ic, "ni");
        FCITX_ASSERT(!hasTab(ic, "nu"));
        FCITX_ASSERT(hasTab(ic, "hao"));
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("End"), false);
        FCITX_ASSERT(!hasTab(ic, "nu"));
        FCITX_ASSERT(hasTab(ic, "hao"));
        clickTab(ic, "hao");
        FCITX_ASSERT(hasTab(ic, "xian"));

        // An incomplete syllable is also a fix. It keeps the initial and
        // leaves the final unrestricted, so "h" no longer becomes a
        // frontend filter.
        ic->reset();
        typeString(testfrontend, uuid, "hhh");
        FCITX_ASSERT(hasTab(ic, "h"));
        FCITX_ASSERT(hasTab(ic, "H"));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "h h h");
        clickTab(ic, "h");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "h h h")
            << ic->inputPanel().preedit().toString();
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));
        // The next "h" is still available.
        FCITX_ASSERT(hasTab(ic, "h"));
        clickTab(ic, "h");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 2)
                         .test(TextFormatFlag::HighLight));

        // The uppercase letter reading is still a separate fix.
        ic->reset();
        typeString(testfrontend, uuid, "hhh");
        clickTab(ic, "H");
        FCITX_ASSERT(!hasCheckedTab(ic));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "H h h")
            << ic->inputPanel().preedit().toString();
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));

        // Turning the switch off restores the checked + frontend filter
        // behavior. Start a fresh composition so the fixes above are gone.
        // Note: changing the addon config resets active input contexts in the
        // test harness, so there is no live composition left to inspect here.
        config.setValueByPath("ContinuousPinyinSelection", "False");
        pinyin->setConfig(config);
        ic->reset();
        typeString(testfrontend, uuid, "xian");
        auto actions = tabActions(ic);
        auto it = std::ranges::find(actions, "xian", &CandidateAction::text);
        FCITX_ASSERT(it != actions.end());
        const int xianId = it->id();
        auto candidateList = ic->inputPanel().candidateList();
        auto *tabbed = candidateList->toTabbed();
        tabbed->triggerTabAction(xianId);
        FCITX_ASSERT(std::ranges::any_of(
            tabbed->tabActions(), [xianId](const auto &action) {
                return action.id() == xianId && action.isChecked();
            }));
        FCITX_ASSERT(findCandidate(ic, "西安") < 0);

        config.setValueByPath("Fuzzy/Correction", "None");
        config.setValueByPath("ContinuousPinyinSelection", "False");
        pinyin->setConfig(config);
    });
}

void testPinyinContinuousShuangpin(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "shuangpin", true);

        RawConfig config;
        config.setValueByPath("ContinuousPinyinSelection", "True");
        config.setValueByPath("ShuangpinProfile", "Ziranma");
        pinyin->setConfig(config);

        // Ziranma: ni = "ni", hao = "hk".
        typeString(testfrontend, uuid, "nihk");
        FCITX_ASSERT(hasTab(ic, "ni"));
        clickTab(ic, "ni");
        FCITX_ASSERT(!hasTab(ic, "ni"));
        FCITX_ASSERT(hasTab(ic, "hao"));
        clickTab(ic, "hao");
        FCITX_ASSERT(!hasTab(ic, "hao"));

        config.setValueByPath("ContinuousPinyinSelection", "False");
        pinyin->setConfig(config);
        instance->setCurrentInputMethod(ic, "pinyin", true);
    });
}

void testPinyinContinuousBackspace(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        RawConfig config;
        config.setValueByPath("Fuzzy/Correction", "QWERTY");
        config.setValueByPath("ContinuousPinyinSelection", "True");
        config.setValueByPath("BackSpaceToUnselect", "True");
        pinyin->setConfig(config);

        auto backspace = [&]() {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);
        };

        // Undo the fix actions in reverse order.
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        clickTab(ic, "hao");
        FCITX_ASSERT(hasTab(ic, "xian"));

        backspace();
        FCITX_ASSERT(hasTab(ic, "hao"));
        FCITX_ASSERT(!hasTab(ic, "xian"));

        backspace();
        FCITX_ASSERT(hasTab(ic, "ni"));
        FCITX_ASSERT(!hasTab(ic, "hao"));

        // With no fixes left, undo removes the last typed character.
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "nu hao xia");

        // A character typed after a fix is undone before the fix itself.
        ic->reset();
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        FCITX_ASSERT(hasTab(ic, "hao"));
        typeString(testfrontend, uuid, "g");
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xiang");
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xian");
        FCITX_ASSERT(hasTab(ic, "hao"));
        backspace();
        FCITX_ASSERT(hasTab(ic, "ni"));
        FCITX_ASSERT(!hasTab(ic, "hao"));

        // Undo a selection first; the dormant fixes become active again.
        ic->reset();
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        clickTab(ic, "hao");
        findAndSelectCandidate(ic, "你好");
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "你好xian");
        // The fixes are dormant under the selection, so nothing is fixed in
        // the preedit.
        FCITX_ASSERT(!formatAtByte(ic->inputPanel().preedit(), 0)
                          .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(hasTab(ic, "xi"));

        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xian");
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 3)
                         .test(TextFormatFlag::HighLight));
        // Continuous mode lists every legal split, so both readings of the
        // remaining "xian" are available.
        FCITX_ASSERT(hasTab(ic, "xian"));
        FCITX_ASSERT(hasTab(ic, "xi"));

        backspace();
        FCITX_ASSERT(hasTab(ic, "hao"));
        backspace();
        FCITX_ASSERT(hasTab(ic, "ni"));
        FCITX_ASSERT(!hasTab(ic, "hao"));

        config.setValueByPath("Fuzzy/Correction", "None");
        config.setValueByPath("ContinuousPinyinSelection", "False");
        config.setValueByPath("BackSpaceToUnselect", "True");
        pinyin->setConfig(config);
    });
}

void testPinyinContinuousUndoOrder(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        RawConfig config;
        config.setValueByPath("Fuzzy/Correction", "QWERTY");
        config.setValueByPath("ContinuousPinyinSelection", "True");
        config.setValueByPath("BackSpaceToUnselect", "True");
        pinyin->setConfig(config);

        auto preedit = [ic]() { return ic->inputPanel().preedit().toString(); };
        auto highlighted = [ic](size_t offset) {
            return formatAtByte(ic->inputPanel().preedit(), offset)
                .test(TextFormatFlag::HighLight);
        };
        auto backspace = [&]() {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);
        };
        // Select by text across all pages; the candidate may not be on the
        // first page.
        auto selectCandidate = [&](std::string_view word) {
            auto candidateList = ic->inputPanel().candidateList();
            FCITX_ASSERT(candidateList);
            auto *bulk = candidateList->toBulk();
            FCITX_ASSERT(bulk);
            for (int i = 0; i < bulk->totalSize(); i++) {
                const auto &candidate = bulk->candidateFromAll(i);
                if (candidate.text().toString() == word) {
                    candidate.select(ic);
                    return;
                }
            }
            FCITX_ASSERT(false) << "Failed to find candidate " << word;
        };

        // Interleave every kind of action:
        //   T T      "nu"
        //   F        fix "ni"
        //   T T T    "hao"
        //   F        fix "hao"
        //   T T T T  "xian"
        //   S        select 你
        //   F        fix "xi"
        //   S        select 好
        //   S        select 西
        // BackSpace must replay it exactly backwards, including the fix
        // applied on top of the first selection and the two selections that
        // follow it.
        typeString(testfrontend, uuid, "nu");
        clickTab(ic, "ni");
        typeString(testfrontend, uuid, "hao");
        clickTab(ic, "hao");
        typeString(testfrontend, uuid, "xian");
        selectCandidate("你");
        clickTab(ic, "xi");
        selectCandidate("好");
        selectCandidate("西");
        FCITX_ASSERT(preedit() == "你好西an") << preedit();
        FCITX_ASSERT(!highlighted(0));

        // S: cancel the 西 selection.
        backspace();
        FCITX_ASSERT(preedit() == "你好xi an") << preedit();
        FCITX_ASSERT(highlighted(6));

        // S: cancel the 好 selection, its dormant fix becomes active again.
        backspace();
        FCITX_ASSERT(preedit() == "你hao xi an") << preedit();
        FCITX_ASSERT(highlighted(3));
        FCITX_ASSERT(highlighted(7));

        // F: remove the "xi" fix, "xian" is one syllable again.
        backspace();
        FCITX_ASSERT(preedit() == "你hao xian") << preedit();
        FCITX_ASSERT(highlighted(3));
        FCITX_ASSERT(!highlighted(7));

        // S: cancel the 你 selection.
        backspace();
        FCITX_ASSERT(preedit() == "ni hao xian") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(highlighted(3));

        // T x4: the characters typed after the "hao" fix.
        backspace();
        FCITX_ASSERT(preedit() == "ni hao xia") << preedit();
        backspace();
        FCITX_ASSERT(preedit() == "ni hao xi") << preedit();
        backspace();
        FCITX_ASSERT(preedit() == "ni hao x") << preedit();
        backspace();
        FCITX_ASSERT(preedit() == "ni hao") << preedit();
        FCITX_ASSERT(!hasTab(ic, "hao"));

        // F: remove the "hao" fix, the tab page moves back to it.
        backspace();
        FCITX_ASSERT(preedit() == "ni hao") << preedit();
        FCITX_ASSERT(hasTab(ic, "hao"));
        FCITX_ASSERT(highlighted(0));

        // T x3: the characters typed before the "hao" fix.
        backspace();
        FCITX_ASSERT(preedit() == "ni ha") << preedit();
        backspace();
        FCITX_ASSERT(preedit() == "ni h") << preedit();
        backspace();
        FCITX_ASSERT(preedit() == "ni") << preedit();
        FCITX_ASSERT(highlighted(0));

        // F: remove the "ni" fix, back to the raw input.
        backspace();
        FCITX_ASSERT(preedit() == "nu") << preedit();
        FCITX_ASSERT(hasTab(ic, "nu"));
        FCITX_ASSERT(!highlighted(0));

        // T x2: the characters typed before the "ni" fix.
        backspace();
        FCITX_ASSERT(preedit() == "n") << preedit();
        backspace();
        FCITX_ASSERT(preedit().empty()) << preedit();

        config.setValueByPath("Fuzzy/Correction", "None");
        config.setValueByPath("ContinuousPinyinSelection", "False");
        config.setValueByPath("BackSpaceToUnselect", "True");
        pinyin->setConfig(config);
    });
}

void testPinyinContinuousBackspaceDeleteMode(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);

        RawConfig config;
        config.setValueByPath("Fuzzy/Correction", "QWERTY");
        config.setValueByPath("ContinuousPinyinSelection", "True");
        config.setValueByPath("BackSpaceToUnselect", "False");
        pinyin->setConfig(config);

        auto backspace = [&]() {
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);
        };

        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        clickTab(ic, "hao");
        FCITX_ASSERT(hasTab(ic, "xian"));

        // Deletions after the last fix only remove characters.
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xia");
        FCITX_ASSERT(hasTab(ic, "xia"));
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xi");
        FCITX_ASSERT(hasTab(ic, "xi"));
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao x");
        FCITX_ASSERT(!hasTab(ic, "xi"));
        FCITX_ASSERT(!hasTab(ic, "xia"));

        // from == fix.end_: the fix is kept, but the syllable is incomplete.
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao");
        FCITX_ASSERT(!hasTab(ic, "hao"));

        // from enters [2,5): libime drops the hao fix on this same keypress.
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni ha");
        FCITX_ASSERT(hasTab(ic, "ha"));
        FCITX_ASSERT(!hasTab(ic, "hao"));

        // The earlier ni fix is only dropped once deletion enters [0,2).
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni h");
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni");
        FCITX_ASSERT(!hasTab(ic, "ni"));
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "n");
        backspace();
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        config.setValueByPath("Fuzzy/Correction", "None");
        config.setValueByPath("ContinuousPinyinSelection", "False");
        config.setValueByPath("BackSpaceToUnselect", "True");
        pinyin->setConfig(config);
    });
}

void testPinyinFixedClientPreedit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        ic->setCapabilityFlags(CapabilityFlag::Preedit);

        RawConfig config;
        config.setValueByPath("Fuzzy/Correction", "QWERTY");
        config.setValueByPath("ContinuousPinyinSelection", "True");

        // ComposingPinyin puts the fixed pinyin into the client preedit.
        config.setValueByPath("PreeditMode", "Composing pinyin");
        config.setValueByPath("PreeditCursorPositionAtBeginning", "False");
        pinyin->setConfig(config);
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        auto clientPreedit = ic->inputPanel().clientPreedit();
        FCITX_ASSERT(clientPreedit.toString() == "ni hao xian")
            << clientPreedit.toString();
        FCITX_ASSERT(
            formatAtByte(clientPreedit, 0).test(TextFormatFlag::HighLight));
        FCITX_ASSERT(
            formatAtByte(clientPreedit, 0).test(TextFormatFlag::Underline));
        FCITX_ASSERT(
            !formatAtByte(clientPreedit, 2).test(TextFormatFlag::HighLight));
        FCITX_ASSERT(
            !formatAtByte(clientPreedit, 3).test(TextFormatFlag::HighLight));
        // The input-window preedit stays empty in this mode.
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        // CommitPreview shows hanzi in the client preedit. It must not be
        // replaced or highlighted. The fixed pinyin is highlighted in the
        // input-window preedit.
        ic->reset();
        config.setValueByPath("PreeditMode", "Commit preview");
        pinyin->setConfig(config);
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        clientPreedit = ic->inputPanel().clientPreedit();
        FCITX_ASSERT(!clientPreedit.toString().empty());
        for (size_t i = 0; i < clientPreedit.size(); i++) {
            FCITX_ASSERT(
                !clientPreedit.formatAt(i).test(TextFormatFlag::HighLight))
                << clientPreedit.toString();
        }
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ni hao xian")
            << ic->inputPanel().preedit().toString();
        FCITX_ASSERT(formatAtByte(ic->inputPanel().preedit(), 0)
                         .test(TextFormatFlag::HighLight));

        config.setValueByPath("PreeditCursorPositionAtBeginning", "True");
        config.setValueByPath("PreeditMode", "Composing pinyin");
        config.setValueByPath("Fuzzy/Correction", "None");
        config.setValueByPath("ContinuousPinyinSelection", "False");
        pinyin->setConfig(config);
    });
}

void testPinyinPreeditClick(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *pinyin = instance->addonManager().addon("pinyin");
        FCITX_ASSERT(pinyin);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        instance->setCurrentInputMethod(ic, "pinyin", true);
        ic->setCapabilityFlags(CapabilityFlag::Preedit);

        RawConfig config;
        config.setValueByPath("Fuzzy/Correction", "QWERTY");
        config.setValueByPath("ContinuousPinyinSelection", "True");
        config.setValueByPath("PreeditMode", "Composing pinyin");
        config.setValueByPath("PreeditCursorPositionAtBeginning", "False");
        pinyin->setConfig(config);

        auto preedit = [ic]() {
            return ic->inputPanel().clientPreedit().toString();
        };
        auto highlighted = [ic](size_t offset) {
            return formatAtByte(ic->inputPanel().clientPreedit(), offset)
                .test(TextFormatFlag::HighLight);
        };
        auto prepare = [&]() {
            ic->reset();
            typeString(testfrontend, uuid, "nuhaoxian");
            clickTab(ic, "ni");
            clickTab(ic, "hao");
            clickTab(ic, "xi");
            FCITX_ASSERT(preedit() == "ni hao xi an") << preedit();
        };

        // Clicking at the end keeps every fix.
        prepare();
        clickPreedit(ic, 12);
        FCITX_ASSERT(preedit() == "ni hao xi an") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(highlighted(3));
        FCITX_ASSERT(highlighted(7));

        // Clicking at the end of "hao" keeps "ni" and "hao" and drops "xi".
        prepare();
        clickPreedit(ic, 6);
        FCITX_ASSERT(preedit() == "ni hao xian") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(highlighted(3));
        FCITX_ASSERT(!highlighted(7));

        // Clicking inside "hao" keeps only "ni".
        prepare();
        clickPreedit(ic, 4);
        FCITX_ASSERT(preedit() == "ni hao xian") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(!highlighted(3));
        FCITX_ASSERT(!highlighted(7));

        // Clicking inside the first fix drops every fix.
        prepare();
        clickPreedit(ic, 1);
        FCITX_ASSERT(preedit() == "nu hao xian") << preedit();
        FCITX_ASSERT(!highlighted(0));
        FCITX_ASSERT(!highlighted(3));
        FCITX_ASSERT(!highlighted(7));

        // Clicking at the end of "xi" keeps all three fixes.
        prepare();
        clickPreedit(ic, 9);
        FCITX_ASSERT(preedit() == "ni hao xi an") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(highlighted(3));
        FCITX_ASSERT(highlighted(7));

        // Clicking inside "xi" keeps "ni" and "hao" and drops "xi".
        prepare();
        clickPreedit(ic, 8);
        FCITX_ASSERT(preedit() == "ni hao xian") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(highlighted(3));
        FCITX_ASSERT(!highlighted(7));

        // Clicking at the end of "ni" keeps "ni" and drops the later fixes.
        prepare();
        clickPreedit(ic, 2);
        FCITX_ASSERT(preedit() == "ni hao xian") << preedit();
        FCITX_ASSERT(highlighted(0));
        FCITX_ASSERT(!highlighted(3));
        FCITX_ASSERT(!highlighted(7));

        // Clicking inside a selected word cancels the selection and drops the
        // fixes from the new edit point to the end.
        ic->reset();
        typeString(testfrontend, uuid, "nuhaoxian");
        clickTab(ic, "ni");
        findAndSelectCandidate(ic, "你好");
        FCITX_ASSERT(preedit() == "你好xian") << preedit();
        clickPreedit(ic, 1);
        FCITX_ASSERT(preedit() == "nu hao xian") << preedit();
        FCITX_ASSERT(!highlighted(0));

        config.setValueByPath("Fuzzy/Correction", "None");
        config.setValueByPath("ContinuousPinyinSelection", "False");
        pinyin->setConfig(config);
    });
}

} // namespace

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
    fcitx::Log::setLogRule("default=5,pinyin=5,*=5");
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    setup(&instance);
    testBasic(&instance);
    testSelectByChar(&instance);
    testUppercase(&instance);
    testForget(&instance);
    testActionInStrokeFilter(&instance);
    testPinyinTabFilter(&instance);
    testPinyinTabFilterWithSeparator(&instance);
    testPinyinContinuousSelection(&instance);
    testPinyinContinuousShuangpin(&instance);
    testPinyinContinuousBackspace(&instance);
    testPinyinContinuousUndoOrder(&instance);
    testPinyinContinuousBackspaceDeleteMode(&instance);
    testPinyinFixedClientPreedit(&instance);
    testPinyinPreeditClick(&instance);
    testPin(&instance);
    testQuickPhraseTrigger(&instance);
    testVQuickPhraseTrigger(&instance);
    testPunctuationWithCursorAtBeginning(&instance);
    testPunctuation(&instance);
    instance.exec();
    endTestEvent.reset();
    return 0;
}
