/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "filelistmodel.h"
#include <QDebug>
#include <QFile>
#include <fcitx-utils/standardpath.h>
#include <fcitxqti18nhelper.h>
#include <fcntl.h>

namespace fcitx {

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {
    loadFileList();
}

FileListModel::~FileListModel() {}

int FileListModel::rowCount(const QModelIndex &) const {
    return fileList_.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= fileList_.size()) {
        return QVariant();
    }

    switch (role) {
    case Qt::DisplayRole: {
        auto name = fileList_[index.row()].first;

        if (name.endsWith(".dict")) {
            name = name.left(name.size() - 5);
        }
        return name;
    }
    case Qt::CheckStateRole: {
        return fileList_[index.row()].second ? Qt::Checked : Qt::Unchecked;
    }
    case Qt::UserRole:
        return fileList_[index.row()].first;
    default:
        break;
    }
    return QVariant();
}

bool FileListModel::setData(const QModelIndex &index, const QVariant &value,
                            int role) {
    if (!index.isValid() || index.row() >= fileList_.size()) {
        return false;
    }

    if (role == Qt::CheckStateRole) {
        if (fileList_[index.row()].second != value.toBool()) {
            fileList_[index.row()].second = value.toBool();
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &index) const {
    if (!index.isValid() || index.row() >= fileList_.size()) {
        return {};
    }

    return Qt::ItemIsUserCheckable | QAbstractListModel::flags(index);
}

void FileListModel::loadFileList() {
    beginResetModel();
    fileList_.clear();
    auto files = StandardPath::global().locate(StandardPath::Type::PkgData,
                                               "pinyin/dictionaries",
                                               filter::Suffix(".dict"));
    std::map<std::string, bool> enableMap;
    for (const auto &file : files) {
        enableMap[file.first] = true;
    }
    auto disableFiles = StandardPath::global().locate(
        StandardPath::Type::PkgData, "pinyin/dictionaries",
        filter::Suffix(".dict.disable"));
    for (const auto &file : disableFiles) {
        // Remove .disable suffix.
        auto dictName = file.first.substr(0, file.first.size() - 8);
        if (auto iter = enableMap.find(dictName); iter != enableMap.end()) {
            iter->second = false;
        }
    }
    for (const auto &file : enableMap) {
        fileList_.append({QString::fromStdString(file.first), file.second});
    }

    endResetModel();
}

int FileListModel::findFile(const QString &lastFileName) {
    auto iter = std::find_if(fileList_.begin(), fileList_.end(),
                             [&lastFileName](const auto &item) {
                                 return item.first == lastFileName;
                             });
    if (iter == fileList_.end()) {
        return 0;
    }
    return std::distance(fileList_.begin(), iter);
}

void FileListModel::save() {
    for (const auto &file : fileList_) {
        auto disableFilePath = stringutils::joinPath(
            StandardPath::global().userDirectory(StandardPath::Type::PkgData),
            "pinyin/dictionaries",
            stringutils::concat(file.first.toStdString(), ".disable"));
        QFile disableFile(QString::fromStdString(disableFilePath));
        if (file.second) {
            disableFile.remove();
        } else {
            disableFile.open(QIODevice::WriteOnly);
            disableFile.close();
        }
    }
}

} // namespace fcitx
