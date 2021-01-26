/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_PINYINDICTMANAGER_H_
#define _PINYINDICTMANAGER_PINYINDICTMANAGER_H_

#include "filelistmodel.h"
#include "pipeline.h"
#include "ui_pinyindictmanager.h"
#include <fcitxqtconfiguiwidget.h>

namespace fcitx {

class PinyinDictManager : public FcitxQtConfigUIWidget,
                          public Ui::PinyinDictManager {
    Q_OBJECT
public:
    PinyinDictManager(QWidget *parent);

    void load() override;
    void save() override;
    QString title() override;
    bool asyncSave() override;
    QString icon() override { return "fcitx-pinyin"; }

private Q_SLOTS:
    void importFromFile();
    void importFromSogou();
    void importFromSogouOnline();
    void removeDict();
    void removeAllDict();
    void clearUserDict();
    void clearAllDict();

private:
    QString confirmImportFileName(const QString &defaultName);
    QString prepareDirectory();
    QString prepareTempFile(const QString &tempFileTemplate);
    QString checkOverwriteFile(const QString &dirName,
                               const QString &importName);
    void reload();

    QAction *importFromFileAction_;
    QAction *importFromSogou_;
    QAction *importFromSogouOnline_;
    QAction *clearUserDictAction_;
    QAction *clearAllDataAction_;

    FileListModel *model_;

    Pipeline *pipeline_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_PINYINDICTMANAGER_H_
