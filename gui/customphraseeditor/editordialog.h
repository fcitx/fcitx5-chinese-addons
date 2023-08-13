/*
 * SPDX-FileCopyrightText: 2012~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _QUICKPHRASE_EDITOR_EDITORDIALOG_H_
#define _QUICKPHRASE_EDITOR_EDITORDIALOG_H_

#include "ui_editordialog.h"
#include <QDialog>

namespace fcitx {
class EditorDialog : public QDialog, public Ui::EditorDialog {
    Q_OBJECT
public:
    explicit EditorDialog(QWidget *parent = 0);
    virtual ~EditorDialog();

    QString key() const;
    QString value() const;
    int order() const;
    void setValue(const QString &s);
    void setKey(const QString &s);
    void setOrder(int order);
};
} // namespace fcitx

#endif // _QUICKPHRASE_EDITOR_EDITORDIALOG_H_
