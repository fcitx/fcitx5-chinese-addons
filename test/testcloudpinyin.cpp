/*
 * SPDX-FileCopyrightText: 2017-2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "cloudpinyin_public.h"
#include "testdir.h"
#include <cassert>
#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <iostream>

int main() {
    fcitx::setupTestingEnvironment(TESTING_BINARY_DIR, {"modules/cloudpinyin"},
                                   {"test", TESTING_SOURCE_DIR "/modules"});
    fcitx::Log::setLogRule("*=5");

    char arg0[] = "testcloudpinyin";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=cloudpinyin";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    int returned = 0;
    instance.eventDispatcher().schedule([&instance, &returned]() {
        auto callback = [&instance, &returned](const std::string &pinyin,
                                               const std::string &hanzi) {
            std::cout << "Pinyin: " << pinyin << std::endl;
            std::cout << "Hanzi: " << hanzi << std::endl;
            returned++;
            if (returned == 1) {
                instance.exit();
            }
        };
        auto *cloudpinyin = instance.addonManager().addon("cloudpinyin", true);
        cloudpinyin->call<fcitx::ICloudPinyin::request>("nihao", callback);
        cloudpinyin->call<fcitx::ICloudPinyin::request>("ceshi", callback);
    });
    instance.exec();

    return 0;
}
