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
#include "pinyindictmanager.h"
#include "browserdialog.h"
#include "filedownloader.h"
#include "log.h"
#include "processrunner.h"
#include "renamefile.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QTemporaryFile>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>

namespace fcitx {

PinyinDictManager::PinyinDictManager(QWidget *parent)
    : FcitxQtConfigUIWidget(parent), model_(new FileListModel(this)),
      pipeline_(new Pipeline(this)) {
    setupUi(this);

    QMenu *menu = new QMenu(this);
    importFromFileAction_ = new QAction(_("From &File"), this);
    importFromSogou_ = new QAction(_("From &Sogou Cell Dictionary File"), this);
    importFromSogouOnline_ =
        new QAction(_("&Browse Sogou Cell Dictionary Online"), this);
    menu->addAction(importFromFileAction_);
    menu->addAction(importFromSogou_);
    menu->addAction(importFromSogouOnline_);
    importButton_->setMenu(menu);

    menu = new QMenu(this);
    clearUserDictAction_ = new QAction(_("&Clear User Data"), this);
    clearAllDataAction_ = new QAction(_("Clear &All Data"), this);
    menu->addAction(clearUserDictAction_);
    menu->addAction(clearAllDataAction_);
    clearDictButton_->setMenu(menu);

    listView_->setModel(model_);

    connect(importFromFileAction_, &QAction::triggered, this,
            &PinyinDictManager::importFromFile);
    connect(importFromSogou_, &QAction::triggered, this,
            &PinyinDictManager::importFromSogou);
    connect(importFromSogouOnline_, &QAction::triggered, this,
            &PinyinDictManager::importFromSogouOnline);
    connect(clearUserDictAction_, &QAction::triggered, this,
            &PinyinDictManager::clearUserDict);
    connect(clearAllDataAction_, &QAction::triggered, this,
            &PinyinDictManager::clearAllDict);

    connect(removeButton_, &QPushButton::clicked, this,
            &PinyinDictManager::removeDict);
    connect(removeAllButton_, &QPushButton::clicked, this,
            &PinyinDictManager::removeAllDict);

    connect(pipeline_, &Pipeline::finished, this, [this]() {
        setEnabled(true);
        reload();
    });

    model_->loadFileList();
}

void PinyinDictManager::load() {}

void PinyinDictManager::save() {}

QString PinyinDictManager::title() { return _("Pinyin dictionary manager"); }

bool PinyinDictManager::asyncSave() { return true; }

QString PinyinDictManager::confirmImportFileName(const QString &defaultName) {
    bool ok;
    auto importName = QInputDialog::getText(
        this, _("Input Dictionary Name"), _("New Dictionary Name:"),
        QLineEdit::Normal, defaultName, &ok);
    if (!ok) {
        return QString();
    }
    return importName;
}

QString PinyinDictManager::prepareDirectory() {
    auto directory = stringutils::joinPath(
        StandardPath::global().userDirectory(StandardPath::Type::PkgData),
        "pinyin/dictionaries");
    if (!fs::makePath(directory)) {
        QMessageBox::warning(this, _("Failed to create directory"),
                             _("Create directory failed. Please check the "
                               "permission or disk space."));
        return "";
    }
    return QString::fromLocal8Bit(directory.data());
}

QString PinyinDictManager::prepareTempFile(const QString &tempFileTemplate) {
    QTemporaryFile tempFile(tempFileTemplate);
    if (!tempFile.open()) {
        QMessageBox::warning(this, _("Failed to create temp file"),
                             _("Creating temp file failed. Please check the "
                               "permission or disk space."));
        return QString();
    }
    tempFile.setAutoRemove(false);
    return tempFile.fileName();
}

QString PinyinDictManager::checkOverwriteFile(const QString &dirName,
                                              const QString &importName) {
    QDir dir(dirName);
    auto fullname = dir.filePath(importName + ".dict");

    if (QFile::exists(fullname)) {
        auto button = QMessageBox::warning(
            this, _("Dictionary already exists"),
            QString(_("%1 already exists, do you want to overwrite this "
                      "dictionary?"))
                .arg(importName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (button == QMessageBox::No) {
            return QString();
        }
    }

    return fullname;
}

void PinyinDictManager::reload() {
    model_->loadFileList();
    saveSubConfig("fcitx://config/addon/pinyin/dictmanager");
}

void PinyinDictManager::importFromFile() {
    QString name =
        QFileDialog::getOpenFileName(this, _("Select Dictionary File"));
    if (name.isEmpty()) {
        return;
    }

    QFileInfo info(name);
    QString importName = info.fileName();

    if (importName.endsWith(".txt")) {
        importName = importName.left(importName.size() - 4);
    }

    importName = confirmImportFileName(importName);
    if (importName.isEmpty()) {
        return;
    }

    auto directory = prepareDirectory();
    if (directory.isEmpty()) {
        return;
    }
    QDir dir(directory);
    auto fullname = dir.filePath(importName + ".dict");
    auto tempFile = prepareTempFile(fullname + "_XXXXXX");
    if (tempFile.isEmpty()) {
        return;
    }

    setEnabled(false);
    pipeline_->reset();
    auto runner = new ProcessRunner(
        "libime_pinyindict", QStringList() << info.fileName() << tempFile,
        tempFile);
    auto rename = new RenameFile(tempFile, fullname);
    pipeline_->addJob(runner);
    pipeline_->addJob(rename);
    pipeline_->start();
}

void PinyinDictManager::importFromSogou() {
    QString name = QFileDialog::getOpenFileName(
        this, _("Select scel file"), QString(), _("Scel file (*.scel)"));
    if (name.isEmpty()) {
        return;
    }

    QFileInfo info(name);
    QString importName = info.fileName();
    if (importName.endsWith(".scel")) {
        importName = importName.left(importName.size() - 5);
    }

    importName = confirmImportFileName(importName);
    if (importName.isEmpty()) {
        return;
    }

    auto directory = prepareDirectory();
    if (directory.isEmpty()) {
        return;
    }
    auto runtimeDirectory =
        StandardPath::global().userDirectory(StandardPath::Type::Runtime);
    if (runtimeDirectory.empty()) {
        QMessageBox::warning(this, _("Failed to get runtime directory"),
                             _("Create directory failed. Please check the "
                               "permission or disk space."));
        return;
    }
    auto fullname = checkOverwriteFile(directory, importName);

    if (fullname.isEmpty()) {
        return;
    }

    auto tempFile = prepareTempFile(fullname + "_XXXXXX");

    QDir runtimeDir(QString::fromLocal8Bit(runtimeDirectory.data(),
                                           runtimeDirectory.size()));
    auto txtFile = prepareTempFile(runtimeDir.filePath("scel_txt_XXXXXX"));
    if (tempFile.isEmpty() || txtFile.isEmpty()) {
        if (!tempFile.isEmpty()) {
            QFile::remove(tempFile);
        }
        return;
    }

    setEnabled(false);
    pipeline_->reset();
    auto scelrunner = new ProcessRunner(
        "scel2org5",
        QStringList() << info.absoluteFilePath() << "-o" << txtFile, txtFile);
    pipeline_->addJob(scelrunner);
    auto dictrunner = new ProcessRunner(
        "libime_pinyindict", QStringList() << txtFile << tempFile, tempFile);
    pipeline_->addJob(dictrunner);
    auto rename = new RenameFile(tempFile, fullname);
    pipeline_->addJob(rename);
    pipeline_->start();
}

void PinyinDictManager::importFromSogouOnline() {
    BrowserDialog dialog(this);
    int result = dialog.exec();
    if (result != QDialog::Accepted) {
        return;
    }

    QString importName = dialog.name();

    importName = confirmImportFileName(importName);
    if (importName.isEmpty()) {
        return;
    }

    auto directory = prepareDirectory();
    if (directory.isEmpty()) {
        return;
    }
    auto runtimeDirectory =
        StandardPath::global().userDirectory(StandardPath::Type::Runtime);
    if (runtimeDirectory.empty()) {
        QMessageBox::warning(this, _("Failed to get runtime directory"),
                             _("Create directory failed. Please check the "
                               "permission or disk space."));
        return;
    }
    auto fullname = checkOverwriteFile(directory, importName);

    if (fullname.isEmpty()) {
        return;
    }

    QDir runtimeDir(QString::fromLocal8Bit(runtimeDirectory.data(),
                                           runtimeDirectory.size()));
    auto tempFile = prepareTempFile(fullname + "_XXXXXX");
    auto txtFile = prepareTempFile(runtimeDir.filePath("scel_txt_XXXXXX"));
    auto scelFile = prepareTempFile(runtimeDir.filePath("scel_XXXXXX"));
    QStringList list;
    list << tempFile << txtFile << scelFile;
    for (const auto &file : list) {
        if (file.isEmpty()) {
            for (const auto &file : list) {
                if (!file.isEmpty()) {
                    QFile::remove(file);
                }
            }

            return;
        }
    }

    setEnabled(false);
    pipeline_->reset();
    auto fileDownloader = new FileDownloader(dialog.url(), scelFile);
    pipeline_->addJob(fileDownloader);
    auto scelrunner = new ProcessRunner(
        "scel2org5", QStringList() << scelFile << "-o" << txtFile, txtFile);
    pipeline_->addJob(scelrunner);
    auto dictrunner = new ProcessRunner(
        "libime_pinyindict", QStringList() << txtFile << tempFile, tempFile);
    pipeline_->addJob(dictrunner);
    auto rename = new RenameFile(tempFile, fullname);
    pipeline_->addJob(rename);
    pipeline_->start();
}

void PinyinDictManager::removeAllDict() {
    int ret = QMessageBox::question(
        this, _("Confirm deletion"),
        QString::fromUtf8(_("Are you sure to delete all dictionaries?")),
        QMessageBox::Ok | QMessageBox::Cancel);

    if (ret != QMessageBox::Ok) {
        return;
    }
    for (int i = 0; i < model_->rowCount(); i++) {
        QModelIndex index = model_->index(i);

        std::string fileName =
            index.data(Qt::UserRole).toString().toLocal8Bit().constData();
        auto fullPath = StandardPath::global().locate(
            StandardPath::Type::PkgData, "pinyin/dictionaries/" + fileName);
        QFile::remove(QString::fromLocal8Bit(fullPath.data(), fullPath.size()));
    }
    reload();
}

void PinyinDictManager::removeDict() {
    QModelIndex index = listView_->currentIndex();
    if (!index.isValid()) {
        return;
    }

    QString curName = index.data(Qt::DisplayRole).toString();
    std::string fileName =
        index.data(Qt::UserRole).toString().toLocal8Bit().constData();
    auto fullPath = StandardPath::global().locate(
        StandardPath::Type::PkgData, "pinyin/dictionaries/" + fileName);

    int ret = QMessageBox::question(
        this, _("Confirm deletion"),
        QString::fromUtf8(_("Are you sure to delete %1?")).arg(curName),
        QMessageBox::Ok | QMessageBox::Cancel);

    if (ret == QMessageBox::Ok) {
        bool ok = QFile::remove(
            QString::fromLocal8Bit(fullPath.data(), fullPath.size()));
        if (!ok) {
            QMessageBox::warning(
                this, _("File Operation Failed"),
                QString::fromUtf8(_("Error while deleting %1.")).arg(curName));
        } else {
            reload();
        }
    }
}

void PinyinDictManager::clearUserDict() {
    emit saveSubConfig("fcitx://config/addon/pinyin/clearuserdict");
}

void PinyinDictManager::clearAllDict() {
    emit saveSubConfig("fcitx://config/addon/pinyin/clearalldict");
}

} // namespace fcitx
