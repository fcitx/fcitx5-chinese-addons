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
#include <qfuturewatcher.h>
#include <vector>

namespace fcitx {

struct CustomPhraseItem {
    QString key;
    QString value;
    int order;
    bool enabled;
};

class CustomPhraseModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum { Column_Enable = 0, Column_Key, Column_Phrase, Column_Order };
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
    void addItem(const QString &key, const QString &value, int order,
                 bool enabled);
    void deleteItem(int row);
    void deleteAllItem();
    QFutureWatcher<bool> *save();
    bool needSave();
    void load();

Q_SIGNALS:
    void needSaveChanged(bool needSave);

private Q_SLOTS:
    void loadFinished();
    void saveFinished();
    void setNeedSave(bool needSave);

private:
    static QList<CustomPhraseItem> parse(const QString &file);
    static bool saveData(const QString &file,
                         const QList<CustomPhraseItem> &list);
    QList<CustomPhraseItem> list_;
    bool needSave_ = false;
    QFutureWatcher<QList<CustomPhraseItem>> *futureWatcher_ = nullptr;
};

std::string customPhraseHelpMessage();

static inline constexpr char customPhraseFileName[] = "pinyin/customphrase";

} // namespace fcitx

#endif // _CUSTOMPHRASEEDITOR_MODEL_H_
