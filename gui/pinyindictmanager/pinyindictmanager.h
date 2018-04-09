//
// Copyright (C) 2018~2018 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//
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
    PinyinDictManager(QWidget *widget);

    void load() override;
    void save() override;
    QString title() override;
    bool asyncSave() override;

private slots:
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
