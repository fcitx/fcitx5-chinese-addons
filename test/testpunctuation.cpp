/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "punctuation_public.h"
#include "testdir.h"
#include <fcitx-utils/log.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>

int main() {
    fcitx::setupTestingEnvironmentPath(
        TESTING_BINARY_DIR, {"bin"},
        {TESTING_BINARY_DIR "/modules", TESTING_SOURCE_DIR "/modules"});
    fcitx::AddonManager manager(TESTING_BINARY_DIR "/modules/punctuation");
    manager.registerDefaultLoader(nullptr);
    manager.load();
    auto *punctuation = manager.addon("punctuation", true);
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
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuationCandidates>(
            "zh_CN", '#') == std::vector<std::string>{"#", "＃"});
    fcitx::RawConfig config;
    config["Entries"]["0"]["Key"] = "*";
    config["Entries"]["0"]["Mapping"] = "X";
    config["Entries"]["0"]["AltMapping"] = "";
    config["Entries"]["1"]["Key"] = "\"";
    config["Entries"]["1"]["Mapping"] = "「";
    config["Entries"]["1"]["AltMapping"] = "」";
    punctuation->setSubConfig("punctuationmap/zh_CN", config);
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '*')
            .first == "X");
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '"')
            .first == "「");
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '"')
            .second == "」");
    FCITX_ASSERT(
        punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", ',')
            .first == "");

    return 0;
}
