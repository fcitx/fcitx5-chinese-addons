/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "punctuation_public.h"
#include "testdir.h"
#include <fcitx-utils/log.h>
#include <fcitx/addonmanager.h>
#include <iostream>

int main() {
    setenv("FCITX_ADDON_DIRS", TESTING_BINARY_DIR "/modules/punctuation", 1);
    setenv("FCITX_DATA_DIRS",
           TESTING_BINARY_DIR "/modules:" TESTING_SOURCE_DIR "/modules", 1);
    fcitx::AddonManager manager(TESTING_BINARY_DIR "/modules/punctuation");
    manager.registerDefaultLoader(nullptr);
    manager.load();
    auto punctuation = manager.addon("punctuation", true);
    FCITX_ASSERT(punctuation);
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", ',')
            .first == "，");
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '"')
            .first == "“");
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '"')
            .second == "”");

    return 0;
}
