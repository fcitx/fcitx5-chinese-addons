/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "filelistmodel.h"
#include <QAbstractListModel>
#include <QDebug>
#include <QFile>
#include <QObject>
#include <QVariant>
#include <Qt>
#include <algorithm>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitxqti18nhelper.h>
#include <fcntl.h>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>

namespace fcitx {

FileListModel::FileListModel(QObject *parent) : QAbstractListModel(parent) {
    loadFileList();
}

FileListModel::~FileListModel() {}

int FileListModel::rowCount(const QModelIndex & /*parent*/) const {
    return fileList_.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= fileList_.size()) {
        return {};
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
    auto files = StandardPaths::global().locate(StandardPathsType::PkgData,
                                                "pinyin/dictionaries",
                                                pathfilter::extension(".dict"));
    std::map<std::string, bool> enableMap;
    for (const auto &file : files) {
        enableMap[file.first] = true;
    }
    auto disableFiles = StandardPaths::global().locate(
        StandardPathsType::PkgData, "pinyin/dictionaries",
        pathfilter::extension(".disable"));
    for (const auto &file : disableFiles) {
        // Remove .disable suffix.
        QString s = QString::fromStdU16String(file.first.u16string());
        auto dictName = s.chopped(8).toStdU16String();
        if (auto iter = enableMap.find(std::filesystem::path(dictName));
            iter != enableMap.end()) {
            iter->second = false;
        }
    }
    for (const auto &file : enableMap) {
        fileList_.append({QString::fromStdString(file.first), file.second});
    }

    endResetModel();
}

int FileListModel::findFile(const QString &lastFileName) {
    auto iter =
        std::ranges::find_if(fileList_, [&lastFileName](const auto &item) {
            return item.first == lastFileName;
        });
    if (iter == fileList_.end()) {
        return 0;
    }
    return std::distance(fileList_.begin(), iter);
}

void FileListModel::save() {
    const auto baseDir =
        StandardPaths::global().userDirectory(StandardPathsType::PkgData) /
        "pinyin/dictionaries";
    for (const auto &file : fileList_) {
        auto disableFilePath =
            baseDir / (file.first.toStdString() + ".disable");
        QFile disableFile(QString::fromStdString(disableFilePath));
        if (file.second) {
            disableFile.remove();
        } else {
            if (fs::makePath(baseDir)) {
                disableFile.open(QIODevice::WriteOnly);
                disableFile.close();
            }
        }
    }
}

} // namespace fcitx
