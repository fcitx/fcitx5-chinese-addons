/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
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
#include "punctuation_public.h"
#include <fcitx/addonmanager.h>
#include <iostream>
#include <cassert>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        return 1;
    }
    setenv("FCITX_ADDON_DIRS", argv[2], 1);
    setenv("FCITX_DATA_DIRS", (std::string(argv[1]) + "/..").c_str(), 1);
    fcitx::AddonManager manager(argv[1]);
    manager.registerDefaultLoader(nullptr);
    manager.load();
    auto punctuation = manager.addon("punctuation");
    assert(punctuation);
    assert(punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", ',', "") == "，");
    assert(punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '"', "") == "“");
    assert(punctuation->call<fcitx::IPunctuation::getPunctuation>("zh_CN", '"', "“") == "”");

    return 0;
}
