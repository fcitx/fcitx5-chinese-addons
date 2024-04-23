/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "pinyinhelper_public.h"
#include "testdir.h"
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <iostream>

int main() {
    setenv("FCITX_ADDON_DIRS", TESTING_BINARY_DIR "/modules/pinyinhelper", 1);
    setenv("FCITX_DATA_DIRS", TESTING_BINARY_DIR "/modules", 1);
    fcitx::AddonManager manager(TESTING_BINARY_DIR "/modules/pinyinhelper");
    manager.registerDefaultLoader(nullptr);
    manager.load();
    auto *pinyinhelper = manager.addon("pinyinhelper", true);
    FCITX_ASSERT(pinyinhelper);
    std::vector<std::string> expect{"nǐ"};
    auto result = pinyinhelper->call<fcitx::IPinyinHelper::lookup>(
        fcitx::utf8::getChar("你"));
    for (auto &s : result) {
        FCITX_INFO() << s << " ";
    }
    FCITX_ASSERT(result == expect);
    auto result2 =
        pinyinhelper->call<fcitx::IPinyinHelper::lookupStroke>("2511", 3);
    for (auto &s : result2) {
        FCITX_INFO()
            << s.first << " "
            << pinyinhelper->call<fcitx::IPinyinHelper::prettyStrokeString>(
                   s.second);
    }
    auto result3 =
        pinyinhelper->call<fcitx::IPinyinHelper::lookupStroke>("szhh", 3);
    FCITX_ASSERT(result2 == result3);

    auto result4 =
        pinyinhelper->call<fcitx::IPinyinHelper::reverseLookupStroke>("你");
    FCITX_ASSERT(result4 == "3235234") << result4;
    auto result5 =
        pinyinhelper->call<fcitx::IPinyinHelper::prettyStrokeString>("54321");
    FCITX_ASSERT(result5 == "𠃍㇏丿丨一");

    return 0;
}
