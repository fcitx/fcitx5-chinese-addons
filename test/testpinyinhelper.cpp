/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see the file COPYING. If not,
 * see <http://www.gnu.org/licenses/>.
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
    auto pinyinhelper = manager.addon("pinyinhelper", true);
    FCITX_ASSERT(pinyinhelper);
    std::vector<std::string> expect{"nǐ"};
    auto result = pinyinhelper->call<fcitx::IPinyinHelper::lookup>(
        fcitx::utf8::getChar("你"));
    for (auto &s : result) {
        FCITX_LOG(Info) << s << " ";
    }
    FCITX_ASSERT(result == expect);
    auto result2 =
        pinyinhelper->call<fcitx::IPinyinHelper::lookupStroke>("2511", 3);
    for (auto &s : result2) {
        FCITX_LOG(Info)
            << s.first << " "
            << pinyinhelper->call<fcitx::IPinyinHelper::prettyStrokeString>(
                   s.second);
    }
    auto result3 =
        pinyinhelper->call<fcitx::IPinyinHelper::lookupStroke>("szhh", 3);
    FCITX_ASSERT(result2 == result3);

    return 0;
}
