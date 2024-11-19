/*
 * SPDX-FileCopyrightText: 2024-2024 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "factory.h"
#include "engine.h"
#include <fcitx-utils/i18n.h>

namespace fcitx {

fcitx::AddonInstance *fcitx::TableEngineFactory::create(AddonManager *manager) {
    registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
    return new TableEngine(manager->instance());
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(table, fcitx::TableEngineFactory)
