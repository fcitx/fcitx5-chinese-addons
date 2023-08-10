/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CUSTOMPHRASEEDITOR_CUSTOMPHRASEEDITOR_H_
#define _CUSTOMPHRASEEDITOR_CUSTOMPHRASEEDITOR_H_

#include "customphrasemodel.h"
#include "ui_customphraseeditor.h"
#include <fcitxqtconfiguiwidget.h>

namespace fcitx {

class CustomPhraseEditor : public FcitxQtConfigUIWidget,
                           public Ui::CustomPhraseEditor {
    Q_OBJECT
public:
    CustomPhraseEditor(QWidget *parent);

    void load() override;
    void save() override;
    QString title() override;
    bool asyncSave() override;
    QString icon() override { return "fcitx-pinyin"; }

private Q_SLOTS:
    void importFromFile();
    void addPhrase();
    void removePhrase();
    void clear();

private:
    void reload();

    QAction *importFromFileAction_;
    QAction *importFromSogou_;
    QAction *importFromSogouOnline_;
    QAction *clearUserDictAction_;
    QAction *clearAllDataAction_;
    CustomPhraseModel *model_;
};

} // namespace fcitx

#endif // _CUSTOMPHRASEEDITOR_CUSTOMPHRASEEDITOR_H_
