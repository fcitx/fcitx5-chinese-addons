/*
 * SPDX-FileCopyrightText: 2012~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "customphrasemodel.h"
#include <QApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcntl.h>
#include <qfuturewatcher.h>
#include <qnamespace.h>

namespace fcitx {

constexpr char customPhraseFileName[] = "pinyin/customphrase";

CustomPhraseModel::CustomPhraseModel(QObject *parent)
    : QAbstractTableModel(parent), needSave_(false) {}

CustomPhraseModel::~CustomPhraseModel() {}

bool CustomPhraseModel::needSave() { return needSave_; }

QVariant CustomPhraseModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == Column_Key) {
            return _("Key");
        } else if (section == Column_Phrase) {
            return _("Phrase");
        } else if (section == Column_Order) {
            return _("Order");
        }
    }
    return QVariant();
}

int CustomPhraseModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return list_.count();
}

int CustomPhraseModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return 4;
}

QVariant CustomPhraseModel::data(const QModelIndex &index, int role) const {
    do {
        if (role == Qt::CheckStateRole && index.column() == Column_Enable) {
            return list_[index.row()].enabled ? Qt::Checked : Qt::Unchecked;
        }
        if ((role == Qt::DisplayRole || role == Qt::EditRole) &&
            index.row() < list_.count()) {
            if (index.column() == Column_Key) {
                return list_[index.row()].key;
            } else if (index.column() == Column_Phrase) {
                return list_[index.row()].value;
            } else if (index.column() == Column_Order) {
                return qAbs(list_[index.row()].order);
            }
        }
    } while (0);
    return QVariant();
}

void CustomPhraseModel::addItem(const QString &key, const QString &word,
                                int order, bool enabled) {
    beginInsertRows(QModelIndex(), list_.size(), list_.size());
    list_.append(CustomPhraseItem{
        .key = key, .value = word, .order = order, .enabled = enabled});
    endInsertRows();
    setNeedSave(true);
}

void CustomPhraseModel::deleteItem(int row) {
    if (row >= list_.count())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    list_.removeAt(row);
    endRemoveRows();
    setNeedSave(true);
}

void CustomPhraseModel::deleteAllItem() {
    if (list_.count()) {
        setNeedSave(true);
    }
    beginResetModel();
    list_.clear();
    endResetModel();
}

Qt::ItemFlags CustomPhraseModel::flags(const QModelIndex &index) const {
    if (!index.isValid())
        return {};

    if (index.column() == Column_Enable) {
        return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable |
               Qt::ItemIsSelectable;
    }
    return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool CustomPhraseModel::setData(const QModelIndex &index, const QVariant &value,
                                int role) {
    if (role == Qt::CheckStateRole && index.column() == Column_Enable) {
        list_[index.row()].enabled = value.toBool();

        Q_EMIT dataChanged(index, index);
        setNeedSave(true);
        return true;
    }

    if (role != Qt::EditRole)
        return false;

    if (index.column() == Column_Key) {
        list_[index.row()].key = value.toString();

        Q_EMIT dataChanged(index, index);
        setNeedSave(true);
        return true;
    } else if (index.column() == Column_Phrase) {
        list_[index.row()].value = value.toString();

        Q_EMIT dataChanged(index, index);
        setNeedSave(true);
        return true;
    } else if (index.column() == Column_Order) {
        list_[index.row()].order = value.toInt();

        Q_EMIT dataChanged(index, index);
        setNeedSave(true);
        return true;
    } else
        return false;
}

void CustomPhraseModel::load() {
    if (futureWatcher_) {
        return;
    }

    beginResetModel();
    setNeedSave(false);
    futureWatcher_ = new QFutureWatcher<QList<CustomPhraseItem>>(this);
    futureWatcher_->setFuture(QtConcurrent::run<QList<CustomPhraseItem>>(
        &CustomPhraseModel::parse, QLatin1String(customPhraseFileName)));
    connect(futureWatcher_, &QFutureWatcherBase::finished, this,
            &CustomPhraseModel::loadFinished);
}

QList<CustomPhraseItem> CustomPhraseModel::parse(const QString &file) {
    QByteArray fileNameArray = file.toLocal8Bit();
    QList<CustomPhraseItem> list;

    do {
        auto fp = fcitx::StandardPath::global().open(
            fcitx::StandardPath::Type::PkgData, fileNameArray.constData(),
            O_RDONLY);
        if (fp.fd() < 0)
            break;

        boost::iostreams::stream_buffer<
            boost::iostreams::file_descriptor_source>
            buffer(fp.fd(),
                   boost::iostreams::file_descriptor_flags::never_close_handle);
        std::istream in(&buffer);
        CustomPhraseDict dict;
        dict.load(in, /*loadDisabled=*/true);
        dict.foreach(
            [&list](const std::string &key, std::vector<CustomPhrase> &items) {
                for (const auto &item : items) {
                    list.append(CustomPhraseItem{
                        .key = QString::fromStdString(key),
                        .value = QString::fromStdString(item.value()),
                        .order = qAbs(item.order()),
                        .enabled = item.order() >= 0});
                }
            });
    } while (0);

    return list;
}

void CustomPhraseModel::loadFinished() {
    list_.append(futureWatcher_->future().result());
    endResetModel();
    futureWatcher_->deleteLater();
    futureWatcher_ = nullptr;
}

QFutureWatcher<bool> *CustomPhraseModel::save(const QString &file) {
    QFutureWatcher<bool> *futureWatcher = new QFutureWatcher<bool>(this);
    futureWatcher->setFuture(
        QtConcurrent::run<bool>(&CustomPhraseModel::saveData, file, list_));
    connect(futureWatcher, &QFutureWatcherBase::finished, this,
            &CustomPhraseModel::saveFinished);
    return futureWatcher;
}

bool CustomPhraseModel::saveData(const QString &file,
                                 const QList<CustomPhraseItem> &list) {
    QByteArray filenameArray = file.toLocal8Bit();
    return StandardPath::global().safeSave(
        StandardPath::Type::PkgData, filenameArray.constData(),
        [&list](int fd) {
            boost::iostreams::stream_buffer<
                boost::iostreams::file_descriptor_source>
                buffer(fd, boost::iostreams::file_descriptor_flags::
                               never_close_handle);
            std::ostream out(&buffer);
            CustomPhraseDict dict;
            dict.save(out);
            return true;
        });
}

void CustomPhraseModel::saveFinished() {
    QFutureWatcher<bool> *watcher =
        static_cast<QFutureWatcher<bool> *>(sender());
    QFuture<bool> future = watcher->future();
    if (future.result()) {
        setNeedSave(false);
    }
    watcher->deleteLater();
}

void CustomPhraseModel::setNeedSave(bool needSave) {
    if (needSave_ != needSave) {
        needSave_ = needSave;
        Q_EMIT needSaveChanged(needSave_);
    }
}
} // namespace fcitx
