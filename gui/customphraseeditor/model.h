/*
 * SPDX-FileCopyrightText: 2023~2023 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _CUSTOMPHRASEEDITOR_MODEL_H_
#define _CUSTOMPHRASEEDITOR_MODEL_H_

#include "../../im/pinyin/customphrase.h"
#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QSet>
#include <QTextStream>

namespace fcitx {

class CustomPhraseModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit CustomPhraseModel(QObject *parent = 0);
    virtual ~CustomPhraseModel();

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    void load(const QString &file, bool append);
    void loadData(QTextStream &stream);
    void addItem(const QString &macro, const QString &word);
    void deleteItem(int row);
    void deleteAllItem();
    QFutureWatcher<bool> *save(const QString &file);
    void saveData(QTextStream &dev);
    bool needSave();

Q_SIGNALS:
    void needSaveChanged(bool needSave);

private Q_SLOTS:
    void loadFinished();
    void saveFinished();

private:
    CustomPhraseDict dict_;
};
} // namespace fcitx

#endif // _CUSTOMPHRASEEDITOR_MODEL_H_
