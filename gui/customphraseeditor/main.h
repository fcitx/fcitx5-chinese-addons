/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CUSTOMPHRASEEDITOR_MAIN_H_
#define _CUSTOMPHRASEEDITOR_MAIN_H_

#include "fcitxqtconfiguiplugin.h"

namespace fcitx {

class CustomPhraseEditorPlugin : public FcitxQtConfigUIPlugin {
    Q_OBJECT
public:
    Q_PLUGIN_METADATA(IID FcitxQtConfigUIFactoryInterface_iid FILE
                      "customphraseeditor.json")
    explicit CustomPhraseEditorPlugin(QObject *parent = nullptr);
    FcitxQtConfigUIWidget *create(const QString &key) override;
};

} // namespace fcitx

#endif // _CUSTOMPHRASEEDITOR_MAIN_H_
