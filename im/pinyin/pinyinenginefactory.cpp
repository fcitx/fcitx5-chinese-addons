/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "pinyinenginefactory.h"
#include "pinyin.h"
#include <fcitx-utils/i18n.h>

namespace fcitx {

fcitx::AddonInstance *
fcitx::PinyinEngineFactory::create(AddonManager *manager) {
    registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
    return new PinyinEngine(manager->instance());
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(pinyin, fcitx::PinyinEngineFactory)
