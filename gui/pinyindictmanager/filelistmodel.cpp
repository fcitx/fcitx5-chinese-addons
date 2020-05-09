/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "filelistmodel.h"
#include <fcitx-utils/standardpath.h>
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
    if (!index.isValid() || index.row() >= fileList_.size())
        return QVariant();

    switch (role) {
    case Qt::DisplayRole: {
        auto name = fileList_[index.row()];

        if (name.endsWith(".dict")) {
            name = name.left(name.size() - 5);
        }
        return name;
    }
    case Qt::UserRole:
        return fileList_[index.row()];
    default:
        break;
    }
    return QVariant();
}

void FileListModel::loadFileList() {
    beginResetModel();
    fileList_.clear();
    auto files = StandardPath::global().multiOpen(
        StandardPath::Type::PkgData, "pinyin/dictionaries", O_RDONLY,
        filter::Suffix(".dict"));
    for (const auto &file : files) {
        fileList_.append(
            QString::fromLocal8Bit(file.first.data(), file.first.size()));
    }

    endResetModel();
}

int FileListModel::findFile(const QString &lastFileName) {
    int idx = fileList_.indexOf(lastFileName);
    if (idx < 0) {
        return 0;
    }
    return idx;
}

} // namespace fcitx
