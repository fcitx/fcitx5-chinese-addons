/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "main.h"
#include "customphraseeditor.h"
#include <fcitx-utils/i18n.h>

namespace fcitx {

CustomPhraseEditorPlugin::CustomPhraseEditorPlugin(QObject *parent)
    : FcitxQtConfigUIPlugin(parent) {
    registerDomain("fcitx5-chinese-addons", FCITX_INSTALL_LOCALEDIR);
}

FcitxQtConfigUIWidget *CustomPhraseEditorPlugin::create(const QString &key) {
    Q_UNUSED(key);
    return new CustomPhraseEditor(nullptr);
}

} // namespace fcitx
