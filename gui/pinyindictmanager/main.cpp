/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "main.h"
#include "pinyindictmanager.h"
#include <fcitx-utils/i18n.h>

namespace fcitx {

PinyinDictManagerPlugin::PinyinDictManagerPlugin(QObject *parent)
    : FcitxQtConfigUIPlugin(parent) {
    registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
}

FcitxQtConfigUIWidget *PinyinDictManagerPlugin::create(const QString &key) {
    Q_UNUSED(key);
    return new PinyinDictManager(nullptr);
}

} // namespace fcitx
