/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_MAIN_H_
#define _PINYINDICTMANAGER_MAIN_H_

#include "fcitxqtconfiguiplugin.h"

namespace fcitx {

class PinyinDictManagerPlugin : public FcitxQtConfigUIPlugin {
    Q_OBJECT
public:
    Q_PLUGIN_METADATA(IID FcitxQtConfigUIFactoryInterface_iid FILE
                      "pinyindictmanager.json")
    explicit PinyinDictManagerPlugin(QObject *parent = nullptr);
    FcitxQtConfigUIWidget *create(const QString &key) override;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_MAIN_H_
