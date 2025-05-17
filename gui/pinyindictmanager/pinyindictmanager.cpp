/*
 * SPDX-FileCopyrightText: 2018-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "pinyindictmanager.h"
#include "config.h"
#include "filelistmodel.h"
#include "pipeline.h"
#include "processrunner.h"
#include "renamefile.h"
#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitxqtconfiguiwidget.h>
#include <qnamespace.h>
#include <string>

#ifdef ENABLE_BROWSER
#include "browserdialog.h"
#include "filedownloader.h"
#endif

namespace fcitx {

PinyinDictManager::PinyinDictManager(QWidget *parent)
    : FcitxQtConfigUIWidget(parent), model_(new FileListModel(this)),
      pipeline_(new Pipeline(this)) {
    setupUi(this);

    auto *menu = new QMenu(this);
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
    connect(openDirectoryButton_, &QPushButton::clicked, this,
            &PinyinDictManager::openUserDirectory);

    connect(pipeline_, &Pipeline::finished, this, [this]() {
        setEnabled(true);
        reload();
    });

    model_->loadFileList();
    connect(model_, &FileListModel::changed, this, [this]() { changed(true); });
}

void PinyinDictManager::load() {}

void PinyinDictManager::save() {
    QMetaObject::invokeMethod(
        this,
        [this]() {
            model_->save();
            Q_EMIT changed(false);
            Q_EMIT saveFinished();
        },
        Qt::QueuedConnection);
}

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
    auto directory =
        StandardPaths::global().userDirectory(StandardPathsType::PkgData) /
        "pinyin/dictionaries";
    if (!fs::makePath(directory)) {
        QMessageBox::warning(this, _("Failed to create directory"),
                             _("Create directory failed. Please check the "
                               "permission or disk space."));
        return "";
    }
    return QString::fromStdU16String(directory.u16string());
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
    auto *runner = new ProcessRunner(
        "libime_pinyindict", QStringList() << info.filePath() << tempFile,
        tempFile);
    auto *rename = new RenameFile(tempFile, fullname);
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
        StandardPaths::global().userDirectory(StandardPathsType::Runtime);
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

    QDir runtimeDir(runtimeDirectory);
    auto txtFile = prepareTempFile(runtimeDir.filePath("scel_txt_XXXXXX"));
    if (tempFile.isEmpty() || txtFile.isEmpty()) {
        if (!tempFile.isEmpty()) {
            QFile::remove(tempFile);
        }
        return;
    }

    setEnabled(false);
    pipeline_->reset();
    auto *scelrunner = new ProcessRunner(
        "scel2org5",
        QStringList() << info.absoluteFilePath() << "-o" << txtFile, txtFile);
    pipeline_->addJob(scelrunner);
    auto *dictrunner = new ProcessRunner(
        "libime_pinyindict", QStringList() << txtFile << tempFile, tempFile);
    pipeline_->addJob(dictrunner);
    auto *rename = new RenameFile(tempFile, fullname);
    pipeline_->addJob(rename);
    pipeline_->start();
}

void PinyinDictManager::importFromSogouOnline() {
#ifdef ENABLE_BROWSER
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
        StandardPaths::global().userDirectory(StandardPathsType::Runtime);
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

    QDir runtimeDir(QString::fromStdWString(runtimeDirectory.wstring()));
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
    auto *fileDownloader = new FileDownloader(dialog.url(), scelFile);
    pipeline_->addJob(fileDownloader);
    auto *scelrunner = new ProcessRunner(
        "scel2org5", QStringList() << scelFile << "-o" << txtFile, txtFile);
    pipeline_->addJob(scelrunner);
    auto *dictrunner = new ProcessRunner(
        "libime_pinyindict", QStringList() << txtFile << tempFile, tempFile);
    pipeline_->addJob(dictrunner);
    auto *rename = new RenameFile(tempFile, fullname);
    pipeline_->addJob(rename);
    pipeline_->start();
#else
    QDesktopServices::openUrl(QUrl("https://pinyin.sogou.com/dict/"));
#endif
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
        auto fullPath = StandardPaths::global().locate(
            StandardPathsType::PkgData, "pinyin/dictionaries/" + fileName);
        QFile::remove(fullPath);
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
    auto fullPath = StandardPaths::global().locate(
        StandardPathsType::PkgData, "pinyin/dictionaries/" + fileName);

    int ret = QMessageBox::question(
        this, _("Confirm deletion"),
        QString::fromUtf8(_("Are you sure to delete %1?")).arg(curName),
        QMessageBox::Ok | QMessageBox::Cancel);

    if (ret == QMessageBox::Ok) {
        bool ok = QFile::remove(fullPath);
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
    Q_EMIT saveSubConfig("fcitx://config/addon/pinyin/clearuserdict");
}

void PinyinDictManager::clearAllDict() {
    Q_EMIT saveSubConfig("fcitx://config/addon/pinyin/clearalldict");
}

void PinyinDictManager::openUserDirectory() {
    QString path = prepareDirectory();
    if (path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace fcitx
