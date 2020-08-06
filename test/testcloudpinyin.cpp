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
#include <fcitx/addonmanager.h>
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        return 1;
    }
    setenv("FCITX_ADDON_DIRS", argv[2], 1);
    setenv("FCITX_DATA_DIRS", (std::string(argv[1]) + "/..").c_str(), 1);
    fcitx::EventLoop loop;
    fcitx::AddonManager manager(argv[1]);
    manager.setEventLoop(&loop);
    manager.registerDefaultLoader(nullptr);
    manager.load();
    auto *cloudpinyin = manager.addon("cloudpinyin", true);
    int returned = 0;
    auto callback = [&loop, &returned](const std::string &pinyin,
                                       const std::string &hanzi) {
        std::cout << "Pinyin: " << pinyin << std::endl;
        std::cout << "Hanzi: " << hanzi << std::endl;
        returned++;
        if (returned == 1) {
            loop.exit();
        }
    };
    cloudpinyin->call<fcitx::ICloudPinyin::request>("nihao", callback);
    cloudpinyin->call<fcitx::ICloudPinyin::request>("ceshi", callback);
    loop.exec();

    return 0;
}
