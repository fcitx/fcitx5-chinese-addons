/*
 * SPDX-FileCopyrightText: 2013-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _PINYINDICTMANAGER_FILELISTMODEL_H_
#define _PINYINDICTMANAGER_FILELISTMODEL_H_

#include <QAbstractListModel>
#include <QStringList>

namespace fcitx {

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit FileListModel(QObject *parent = nullptr);
    ~FileListModel() override;

    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void loadFileList();
    void save();
    int findFile(const QString &lastFileName);

Q_SIGNALS:
    void changed();

private:
    QList<QPair<QString, bool>> fileList_;
};

} // namespace fcitx

#endif // _PINYINDICTMANAGER_FILELISTMODEL_H_
